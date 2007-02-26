/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Copyright (c) ENST 2000-200X
 *					All rights reserved
 *
 *  This file is part of GPAC / svg2bifs application
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *   
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <gpac/scene_manager.h>
#include <gpac/xml.h>
#include <gpac/internal/scenegraph_dev.h>
#include <gpac/nodes_svg3.h>
#include <gpac/nodes_mpeg4.h>

typedef struct {
	GF_SAXParser *sax_parser;
	
	GF_SceneGraph *svg_sg;
	GF_Node *svg_parent;
	SVG3AllAttributes all_atts;
	SVGPropertiesPointers svg_props;

	GF_SceneGraph *bifs_sg;
	GF_Node *bifs_parent;
	GF_Node *bifs_text_node;

} SVG2BIFS_Converter;

static GF_Node *create_appearance(SVGPropertiesPointers *svg_props, GF_SceneGraph *sg)
{
	M_Appearance *app;
	M_Material2D *mat;
	M_XLineProperties *xlp;
	M_RadialGradient *rg;
	M_LinearGradient *lg;

	app = (M_Appearance *)gf_node_new(sg, TAG_MPEG4_Appearance);
	
	app->material = gf_node_new(sg, TAG_MPEG4_Material2D);
	mat = (M_Material2D *)app->material;
	gf_node_register((GF_Node*)mat, (GF_Node*)app);
	
	if (svg_props->fill->type == SVG_PAINT_NONE) {
		mat->filled = 0;	
	} else {
		mat->filled = 1;
		if (svg_props->fill->type == SVG_PAINT_COLOR) {
			if (svg_props->fill->color.type == SVG_COLOR_RGBCOLOR) {
				mat->emissiveColor.red = svg_props->fill->color.red;
				mat->emissiveColor.green = svg_props->fill->color.green;
				mat->emissiveColor.blue = svg_props->fill->color.blue;
			} else if (svg_props->fill->color.type == SVG_COLOR_CURRENTCOLOR) {
				mat->emissiveColor.red = svg_props->color->color.red;
				mat->emissiveColor.green = svg_props->color->color.green;
				mat->emissiveColor.blue = svg_props->color->color.blue;
			} else {
				/* WARNING */
				mat->emissiveColor.red = 0;
				mat->emissiveColor.green = 0;
				mat->emissiveColor.blue = 0;
			}
		} else { // SVG_PAINT_URI
			/* TODO: gradient or solidcolor */
		}
	}

	mat->transparency = FIX_ONE - svg_props->fill_opacity->value;

	if (svg_props->stroke->type != SVG_PAINT_NONE && 
		svg_props->stroke_width->value != 0) {
		mat->lineProps = gf_node_new(sg, TAG_MPEG4_XLineProperties);
		xlp = (M_XLineProperties *)mat->lineProps;	
		gf_node_register((GF_Node*)xlp, (GF_Node*)mat);
		
		xlp->width = svg_props->stroke_width->value;

		if (svg_props->stroke->type == SVG_PAINT_COLOR) {
			if (svg_props->stroke->color.type == SVG_COLOR_RGBCOLOR) {
				xlp->lineColor.red = svg_props->stroke->color.red;
				xlp->lineColor.green = svg_props->stroke->color.green;
				xlp->lineColor.blue = svg_props->stroke->color.blue;
			} else if (svg_props->stroke->color.type == SVG_COLOR_CURRENTCOLOR) {
				xlp->lineColor.red = svg_props->color->color.red;
				xlp->lineColor.green = svg_props->color->color.green;
				xlp->lineColor.blue = svg_props->color->color.blue;
			} else {
				/* WARNING */
				xlp->lineColor.red = 0;
				xlp->lineColor.green = 0;
				xlp->lineColor.blue = 0;
			}
		} else { // SVG_PAINT_URI
			/* TODO: xlp->texture =  ... */
		}
		xlp->transparency = FIX_ONE - svg_props->stroke_opacity->value;
		xlp->lineCap = *svg_props->stroke_linecap;
		xlp->lineJoin = *svg_props->stroke_linejoin;
		xlp->miterLimit = svg_props->stroke_miterlimit->value;
	}
	
	return (GF_Node*)app;
}

u32 svg3_apply_inheritance(SVG3AllAttributes *all_atts, SVGPropertiesPointers *render_svg_props) 
{
	u32 inherited_flags_mask = GF_SG_NODE_DIRTY | GF_SG_CHILD_DIRTY;
	if(!all_atts || !render_svg_props) return ~inherited_flags_mask;

	if (all_atts->audio_level && all_atts->audio_level->type != SVG_NUMBER_INHERIT)
		render_svg_props->audio_level = all_atts->audio_level;	
	
	if (all_atts->color && all_atts->color->color.type != SVG_COLOR_INHERIT) {
		render_svg_props->color = all_atts->color;
	} else {
		inherited_flags_mask |= GF_SG_SVG_COLOR_DIRTY;
	}
	if (all_atts->color_rendering && *(all_atts->color_rendering) != SVG_RENDERINGHINT_INHERIT) {
		render_svg_props->color_rendering = all_atts->color_rendering;
	}
	if (all_atts->display && *(all_atts->display) != SVG_DISPLAY_INHERIT) {
		render_svg_props->display = all_atts->display;
	}
	if (all_atts->display_align && *(all_atts->display_align) != SVG_DISPLAYALIGN_INHERIT) {
		render_svg_props->display_align = all_atts->display_align;
	} else {
		inherited_flags_mask |= GF_SG_SVG_DISPLAYALIGN_DIRTY;
	}
	if (all_atts->fill && all_atts->fill->type != SVG_PAINT_INHERIT) {
		render_svg_props->fill = all_atts->fill;
		if (all_atts->fill->type == SVG_PAINT_COLOR && 
			all_atts->fill->color.type == SVG_COLOR_CURRENTCOLOR &&
			(inherited_flags_mask & GF_SG_SVG_COLOR_DIRTY)) {
			inherited_flags_mask |= GF_SG_SVG_FILL_DIRTY;
		}
	} else {
		inherited_flags_mask |= GF_SG_SVG_FILL_DIRTY;
	}
	if (all_atts->fill_opacity && all_atts->fill_opacity->type != SVG_NUMBER_INHERIT) {
		render_svg_props->fill_opacity = all_atts->fill_opacity;
	} else {
		inherited_flags_mask |= GF_SG_SVG_FILLOPACITY_DIRTY;
	}
	if (all_atts->fill_rule && *(all_atts->fill_rule) != SVG_FILLRULE_INHERIT) {
		render_svg_props->fill_rule = all_atts->fill_rule;
	} else {
		inherited_flags_mask |= GF_SG_SVG_FILLRULE_DIRTY;
	}
	if (all_atts->font_family && all_atts->font_family->type != SVG_FONTFAMILY_INHERIT) {
		render_svg_props->font_family = all_atts->font_family;
	} else {
		inherited_flags_mask |= GF_SG_SVG_FONTFAMILY_DIRTY;
	}
	if (all_atts->font_size && all_atts->font_size->type != SVG_NUMBER_INHERIT) {
		render_svg_props->font_size = all_atts->font_size;
	} else {
		inherited_flags_mask |= GF_SG_SVG_FONTSIZE_DIRTY;
	}
	if (all_atts->font_style && *(all_atts->font_style) != SVG_FONTSTYLE_INHERIT) {
		render_svg_props->font_style = all_atts->font_style;
	} else {
		inherited_flags_mask |= GF_SG_SVG_FONTSTYLE_DIRTY;
	}
	if (all_atts->font_variant && *(all_atts->font_variant) != SVG_FONTVARIANT_INHERIT) {
		render_svg_props->font_variant = all_atts->font_variant;
	} else {
		inherited_flags_mask |= GF_SG_SVG_FONTVARIANT_DIRTY;
	}
	if (all_atts->font_weight && *(all_atts->font_weight) != SVG_FONTWEIGHT_INHERIT) {
		render_svg_props->font_weight = all_atts->font_weight;
	} else {
		inherited_flags_mask |= GF_SG_SVG_FONTWEIGHT_DIRTY;
	}
	if (all_atts->image_rendering && *(all_atts->image_rendering) != SVG_RENDERINGHINT_INHERIT) {
		render_svg_props->image_rendering = all_atts->image_rendering;
	}
	if (all_atts->line_increment && all_atts->line_increment->type != SVG_NUMBER_INHERIT) {
		render_svg_props->line_increment = all_atts->line_increment;
	} else {
		inherited_flags_mask |= GF_SG_SVG_LINEINCREMENT_DIRTY;
	}
	if (all_atts->opacity && all_atts->opacity->type != SVG_NUMBER_INHERIT) {
		render_svg_props->opacity = all_atts->opacity;
	} else {
		inherited_flags_mask |= GF_SG_SVG_OPACITY_DIRTY;
	}
	if (all_atts->pointer_events && *(all_atts->pointer_events) != SVG_POINTEREVENTS_INHERIT) {
		render_svg_props->pointer_events = all_atts->pointer_events;
	}
	if (all_atts->shape_rendering && *(all_atts->shape_rendering) != SVG_RENDERINGHINT_INHERIT) {
		render_svg_props->shape_rendering = all_atts->shape_rendering;
	}
	if (all_atts->solid_color && all_atts->solid_color->type != SVG_PAINT_INHERIT) {
		render_svg_props->solid_color = all_atts->solid_color;		
		if (all_atts->solid_color->type == SVG_PAINT_COLOR && 
			all_atts->solid_color->color.type == SVG_COLOR_CURRENTCOLOR &&
			(inherited_flags_mask & GF_SG_SVG_COLOR_DIRTY)) {
			inherited_flags_mask |= GF_SG_SVG_SOLIDCOLOR_DIRTY;
		}
	} else {
		inherited_flags_mask |= GF_SG_SVG_SOLIDCOLOR_DIRTY;
	}
	if (all_atts->solid_opacity && all_atts->solid_opacity->type != SVG_NUMBER_INHERIT) {
		render_svg_props->solid_opacity = all_atts->solid_opacity;
	} else {
		inherited_flags_mask |= GF_SG_SVG_SOLIDOPACITY_DIRTY;
	}
	if (all_atts->stop_color && all_atts->stop_color->type != SVG_PAINT_INHERIT) {
		render_svg_props->stop_color = all_atts->stop_color;
		if (all_atts->stop_color->type == SVG_PAINT_COLOR && 
			all_atts->stop_color->color.type == SVG_COLOR_CURRENTCOLOR &&
			(inherited_flags_mask & GF_SG_SVG_COLOR_DIRTY)) {
			inherited_flags_mask |= GF_SG_SVG_STOPCOLOR_DIRTY;
		}
	} else {
		inherited_flags_mask |= GF_SG_SVG_STOPCOLOR_DIRTY;
	}
	if (all_atts->stop_opacity && all_atts->stop_opacity->type != SVG_NUMBER_INHERIT) {
		render_svg_props->stop_opacity = all_atts->stop_opacity;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STOPOPACITY_DIRTY;
	}
	if (all_atts->stroke && all_atts->stroke->type != SVG_PAINT_INHERIT) {
		render_svg_props->stroke = all_atts->stroke;
		if (all_atts->stroke->type == SVG_PAINT_COLOR && 
			all_atts->stroke->color.type == SVG_COLOR_CURRENTCOLOR &&
			(inherited_flags_mask & GF_SG_SVG_COLOR_DIRTY)) {
			inherited_flags_mask |= GF_SG_SVG_STROKE_DIRTY;
		}
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKE_DIRTY;
	}
	if (all_atts->stroke_dasharray && all_atts->stroke_dasharray->type != SVG_STROKEDASHARRAY_INHERIT) {
		render_svg_props->stroke_dasharray = all_atts->stroke_dasharray;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKEDASHARRAY_DIRTY;
	}
	if (all_atts->stroke_dashoffset && all_atts->stroke_dashoffset->type != SVG_NUMBER_INHERIT) {
		render_svg_props->stroke_dashoffset = all_atts->stroke_dashoffset;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKEDASHOFFSET_DIRTY;
	}
	if (all_atts->stroke_linecap && *(all_atts->stroke_linecap) != SVG_STROKELINECAP_INHERIT) {
		render_svg_props->stroke_linecap = all_atts->stroke_linecap;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKELINECAP_DIRTY;
	}
	if (all_atts->stroke_linejoin && *(all_atts->stroke_linejoin) != SVG_STROKELINEJOIN_INHERIT) {
		render_svg_props->stroke_linejoin = all_atts->stroke_linejoin;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKELINEJOIN_DIRTY;
	}
	if (all_atts->stroke_miterlimit && all_atts->stroke_miterlimit->type != SVG_NUMBER_INHERIT) {
		render_svg_props->stroke_miterlimit = all_atts->stroke_miterlimit;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKEMITERLIMIT_DIRTY;
	}
	if (all_atts->stroke_opacity && all_atts->stroke_opacity->type != SVG_NUMBER_INHERIT) {
		render_svg_props->stroke_opacity = all_atts->stroke_opacity;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKEOPACITY_DIRTY;
	}
	if (all_atts->stroke_width && all_atts->stroke_width->type != SVG_NUMBER_INHERIT) {
		render_svg_props->stroke_width = all_atts->stroke_width;
	} else {
		inherited_flags_mask |= GF_SG_SVG_STROKEWIDTH_DIRTY;
	}
	if (all_atts->text_align && *(all_atts->text_align) != SVG_TEXTALIGN_INHERIT) {
		render_svg_props->text_align = all_atts->text_align;
	} else {
		inherited_flags_mask |= GF_SG_SVG_TEXTALIGN_DIRTY;
	}
	if (all_atts->text_anchor && *(all_atts->text_anchor) != SVG_TEXTANCHOR_INHERIT) {
		render_svg_props->text_anchor = all_atts->text_anchor;
	} else {
		inherited_flags_mask |= GF_SG_SVG_TEXTANCHOR_DIRTY;
	}
	if (all_atts->text_rendering && *(all_atts->text_rendering) != SVG_RENDERINGHINT_INHERIT) {
		render_svg_props->text_rendering = all_atts->text_rendering;
	}
	if (all_atts->vector_effect && *(all_atts->vector_effect) != SVG_VECTOREFFECT_INHERIT) {
		render_svg_props->vector_effect = all_atts->vector_effect;
	} else {
		inherited_flags_mask |= GF_SG_SVG_VECTOREFFECT_DIRTY;
	}
	if (all_atts->viewport_fill && all_atts->viewport_fill->type != SVG_PAINT_INHERIT) {
		render_svg_props->viewport_fill = all_atts->viewport_fill;		
	}
	if (all_atts->viewport_fill_opacity && all_atts->viewport_fill_opacity->type != SVG_NUMBER_INHERIT) {
		render_svg_props->viewport_fill_opacity = all_atts->viewport_fill_opacity;
	}
	if (all_atts->visibility && *(all_atts->visibility) != SVG_VISIBILITY_INHERIT) {
		render_svg_props->visibility = all_atts->visibility;
	}
	return inherited_flags_mask;
}

static GF_Node *add_transform(SVG2BIFS_Converter *converter, GF_Node *node)
{
	M_TransformMatrix2D *tr = (M_TransformMatrix2D*)gf_node_new(converter->bifs_sg, TAG_MPEG4_TransformMatrix2D);
	gf_node_register((GF_Node *)tr, node);
	gf_node_list_add_child(&((GF_ParentNode*)node)->children, (GF_Node *)tr);
	if (converter->all_atts.transform) {
		SVG_Transform *svg_tr = converter->all_atts.transform;
		tr->mxx = svg_tr->mat.m[0];
		tr->mxy = svg_tr->mat.m[1];
		tr->tx  = svg_tr->mat.m[2];
		tr->myx = svg_tr->mat.m[3];
		tr->myy = svg_tr->mat.m[4];
		tr->ty  = svg_tr->mat.m[5];
	} 
	return (GF_Node *)tr;

}

static void svg2bifs_node_start(void *sax_cbck, const char *name, const char *name_space, const GF_XMLAttribute *attributes, u32 nb_attributes)
{
	u32 i;
	SVG2BIFS_Converter *converter = (SVG2BIFS_Converter *)sax_cbck;
	SVGPropertiesPointers *backup_props;
	char *id_string = NULL;

	u32	tag = gf_node_svg3_type_by_class_name(name);
	SVG3Element *elt = (SVG3Element*)gf_node_new(converter->svg_sg, tag);
	if (!gf_sg_get_root_node(converter->svg_sg)) {
		gf_node_register((GF_Node *)elt, NULL);
		gf_sg_set_root_node(converter->svg_sg, (GF_Node *)elt);
	} else {
		gf_node_register((GF_Node *)elt, converter->svg_parent);	
		//gf_node_list_add_child(&((GF_ParentNode*)converter->svg_parent)->children, (GF_Node *)elt);
	}
	converter->svg_parent = (GF_Node *)elt;
	
//	fprintf(stdout, "Converting %s\n", gf_node_get_class_name((GF_Node *)elt));
//	if (converter->bifs_parent) fprintf(stdout, "%s\n", gf_node_get_class_name(converter->bifs_parent));

	for (i=0; i<nb_attributes; i++) {
		GF_XMLAttribute *att = (GF_XMLAttribute *)&attributes[i];
		if (!att->value || !strlen(att->value)) continue;

		if (!stricmp(att->name, "style")) {
			gf_svg_parse_style((GF_Node *)elt, att->value);
		} else if (!stricmp(att->name, "id") || !stricmp(att->name, "xml:id")) {
			gf_svg_parse_element_id((GF_Node *)elt, att->value, 0);
			id_string = att->value;
		} else {
			GF_FieldInfo info;
			if (gf_node_get_field_by_name((GF_Node *)elt, att->name, &info)==GF_OK) {
				gf_svg_parse_attribute((GF_Node *)elt, &info, att->value, 0);
			} else {
				fprintf(stdout, "Skipping attribute %s\n", att->name);
			}
		}
	}

	memset(&converter->all_atts, 0, sizeof(SVG3AllAttributes));
	gf_svg3_fill_all_attributes(&converter->all_atts, elt);
	
	backup_props = gf_malloc(sizeof(SVGPropertiesPointers));
	memcpy(backup_props, &converter->svg_props, sizeof(SVGPropertiesPointers));
	gf_node_set_private((GF_Node *)elt, backup_props);

	svg3_apply_inheritance(&converter->all_atts, &converter->svg_props);

	if (!gf_sg_get_root_node(converter->bifs_sg)) {
		if (tag == TAG_SVG3_svg) {
			GF_Node *node, *child;

			converter->bifs_sg->usePixelMetrics = 1;
			if (converter->all_atts.width && converter->all_atts.width->type == SVG_NUMBER_VALUE) {
				converter->bifs_sg->width = FIX2INT(converter->all_atts.width->value);
			} else {
				converter->bifs_sg->width = 320;
			}
			if (converter->all_atts.height && converter->all_atts.height->type == SVG_NUMBER_VALUE) {
				converter->bifs_sg->height = FIX2INT(converter->all_atts.height->value);
			} else {
				converter->bifs_sg->height = 200;
			}

			node = gf_node_new(converter->bifs_sg, TAG_MPEG4_OrderedGroup);
			gf_node_register(node, NULL);
			gf_sg_set_root_node(converter->bifs_sg, node);

			/* SVG to BIFS coordinate transformation */
			child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Viewport);
			gf_node_register(child, node);
			gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
			if (converter->all_atts.viewBox) {
				M_Viewport *vp = (M_Viewport*)child;
				vp->size.x = converter->all_atts.viewBox->width;
				vp->size.y = converter->all_atts.viewBox->height;
				vp->position.x = converter->all_atts.viewBox->x+converter->all_atts.viewBox->width/2;
				vp->position.y = -(converter->all_atts.viewBox->y+converter->all_atts.viewBox->height/2);
			} else {
				M_Viewport *vp = (M_Viewport*)child;
				vp->size.x = INT2FIX(converter->bifs_sg->width);
				vp->size.y = INT2FIX(converter->bifs_sg->height);
				vp->position.x = INT2FIX(converter->bifs_sg->width)/2;
				vp->position.y = -INT2FIX(converter->bifs_sg->height)/2;
			}

			child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Background2D);
			gf_node_register(child, node);
			gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
			{
				M_Background2D *b = (M_Background2D *)child;
				b->backColor.red = FIX_ONE;
				b->backColor.green = FIX_ONE;				
				b->backColor.blue = FIX_ONE;
			}

			child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Transform2D);
			gf_node_register(child, node);
			gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
			node = child;
			child = NULL;
			{
				M_Transform2D *tr = (M_Transform2D *)node;
				tr->scale.y = -FIX_ONE;
			}
			converter->bifs_parent = node;
		}
	} else {
		GF_Node *node, *child;
		
		node = converter->bifs_parent;

		switch(tag) {
		case TAG_SVG3_g:
			{
				if (converter->all_atts.transform) {
					node = add_transform(converter, node);
					converter->bifs_parent = node;
				} else {
					M_Group *g = (M_Group*)gf_node_new(converter->bifs_sg, TAG_MPEG4_Group);
					gf_node_register((GF_Node *)g, node);
					gf_node_list_add_child(&((GF_ParentNode*)node)->children, (GF_Node *)g);
					node = (GF_Node *)g;
					converter->bifs_parent = node;
				}
			}
			break;
		case TAG_SVG3_rect:
			{
				Bool is_parent_set = 0;
				if (converter->all_atts.transform) {
					node = add_transform(converter, node);
					converter->bifs_parent = node;
					is_parent_set = 1;
				} 
				if (converter->all_atts.x || converter->all_atts.y) {
					child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Transform2D);
					gf_node_register(child, node);
					gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
					node = child;
					child = NULL;
					if (!is_parent_set) {
						converter->bifs_parent = node;
						is_parent_set = 1;
					}
					{
						M_Transform2D *tr = (M_Transform2D *)node;
						if (converter->all_atts.x) tr->translation.x = converter->all_atts.x->value + (converter->all_atts.width?converter->all_atts.width->value/2:0);
						if (converter->all_atts.y) tr->translation.y = converter->all_atts.y->value + (converter->all_atts.height?converter->all_atts.height->value/2:0);
					}
				} 
				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Shape);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				if (!is_parent_set) converter->bifs_parent = node;
				{
					M_Shape *shape = (M_Shape *)node;
					shape->geometry = gf_node_new(converter->bifs_sg, TAG_MPEG4_Rectangle);
					gf_node_register(shape->geometry, (GF_Node *)shape);
					{
						M_Rectangle *rect = (M_Rectangle *)shape->geometry;
						if (converter->all_atts.width) rect->size.x = converter->all_atts.width->value;
						if (converter->all_atts.height) rect->size.y = converter->all_atts.height->value;					
					}			
					
					shape->appearance = create_appearance(&converter->svg_props, converter->bifs_sg);
					gf_node_register(shape->appearance, (GF_Node *)shape);
				}
			}
			break;
		case TAG_SVG3_path:
			{
				Bool is_parent_set = 0;
				if (converter->all_atts.transform) {
					node = add_transform(converter, node);
					converter->bifs_parent = node;
					is_parent_set = 1;
				} 
				if (converter->all_atts.x || converter->all_atts.y) {
					child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Transform2D);
					gf_node_register(child, node);
					gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
					node = child;
					child = NULL;
					if (!is_parent_set) {
						converter->bifs_parent = node;
						is_parent_set = 1;
					}
					{
						M_Transform2D *tr = (M_Transform2D *)node;
						if (converter->all_atts.x) tr->translation.x = converter->all_atts.x->value;
						if (converter->all_atts.y) tr->translation.y = converter->all_atts.y->value;
					}
				} 
				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Shape);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				if (!is_parent_set) converter->bifs_parent = node;
				{
					M_Shape *shape = (M_Shape *)node;
					shape->geometry = gf_node_new(converter->bifs_sg, TAG_MPEG4_XCurve2D);
					gf_node_register(shape->geometry, (GF_Node *)shape);
					if (converter->all_atts.d) {
						M_Coordinate2D *c2d;
						M_XCurve2D *xc = (M_XCurve2D *)shape->geometry;
						u32 i, j, c;

						xc->point = gf_node_new(converter->bifs_sg, TAG_MPEG4_Coordinate2D);
						c2d = (M_Coordinate2D *)xc->point;
						gf_node_register(xc->point, (GF_Node *)xc);
						
						gf_sg_vrml_mf_alloc(&c2d->point, GF_SG_VRML_MFVEC2F, converter->all_atts.d->n_points);
						j= 0;
						for (i = 0; i < converter->all_atts.d->n_points; i++) {
							if (converter->all_atts.d->tags[i] != GF_PATH_CLOSE || i == converter->all_atts.d->n_points-1) {
								c2d->point.vals[j] = converter->all_atts.d->points[i];
								j++;
							}
						}
						c2d->point.count = j;

						gf_sg_vrml_mf_alloc(&xc->type, GF_SG_VRML_MFINT32, converter->all_atts.d->n_points);
						c = 0;
						j = 0;
						xc->type.vals[0] = 0;
						for (i = 1; i < converter->all_atts.d->n_points; i++) {
							switch(converter->all_atts.d->tags[i]) {
							case GF_PATH_CURVE_ON:
								if (c < converter->all_atts.d->n_contours &&
									i-1 == converter->all_atts.d->contours[c]) {
									xc->type.vals[j] = 0;
									c++;
								} else {
									xc->type.vals[j] = 1;
								}
								break;
							case GF_PATH_CURVE_CUBIC:
								xc->type.vals[j] = 2;
								i+=2;
								break;
							case GF_PATH_CLOSE:
								xc->type.vals[j] = 6;
								break;
							case GF_PATH_CURVE_CONIC:
								xc->type.vals[j] = 7;
								i++;
								break;
							}
							j++;
						}
						xc->type.count = j;
					}			
					
					shape->appearance = create_appearance(&converter->svg_props, converter->bifs_sg);
					gf_node_register(shape->appearance, (GF_Node *)shape);
				}
			}
			break;
		case TAG_SVG3_polyline:
			{
				Bool is_parent_set = 0;
				if (converter->all_atts.transform) {
					node = add_transform(converter, node);
					converter->bifs_parent = node;
					is_parent_set = 1;
				} 

				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Shape);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				if (!is_parent_set) converter->bifs_parent = node;
				{
					M_Shape *shape = (M_Shape *)node;
					shape->geometry = gf_node_new(converter->bifs_sg, TAG_MPEG4_IndexedFaceSet2D);
					gf_node_register(shape->geometry, (GF_Node *)shape);
					if (converter->all_atts.points) {
						M_Coordinate2D *c2d;
						M_IndexedFaceSet2D *ifs = (M_IndexedFaceSet2D *)shape->geometry;
						u32 i;

						ifs->coord = gf_node_new(converter->bifs_sg, TAG_MPEG4_Coordinate2D);
						c2d = (M_Coordinate2D *)ifs->coord;
						gf_node_register(ifs->coord, (GF_Node *)ifs);
						
						gf_sg_vrml_mf_alloc(&c2d->point, GF_SG_VRML_MFVEC2F, gf_list_count(*converter->all_atts.points));
						for (i = 0; i < gf_list_count(*converter->all_atts.points); i++) {
							SVG_Point *p = (SVG_Point *)gf_list_get(*converter->all_atts.points, i);
							c2d->point.vals[i].x = p->x;
							c2d->point.vals[i].y = p->y;
						}						
					}			
					
					shape->appearance = create_appearance(&converter->svg_props, converter->bifs_sg);
					gf_node_register(shape->appearance, (GF_Node *)shape);
				}
			}
			break;
		case TAG_SVG3_text:
			{
				Bool is_parent_set = 0;
				if (converter->all_atts.transform) {
					node = add_transform(converter, node);
					converter->bifs_parent = node;
					is_parent_set = 1;
				}

				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Transform2D);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				{
					M_Transform2D *tr = (M_Transform2D *)child;
					if (converter->all_atts.text_x) tr->translation.x = ((SVG_Coordinate *)gf_list_get(*converter->all_atts.text_x, 0))->value;
					if (converter->all_atts.text_y) tr->translation.y = ((SVG_Coordinate *)gf_list_get(*converter->all_atts.text_y, 0))->value;
					tr->scale.y = -FIX_ONE;
				}
				node = child;
				child = NULL;
				if (!is_parent_set) {
					converter->bifs_parent = node;
					is_parent_set = 1;
				}

				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Shape);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				if (!is_parent_set) converter->bifs_parent = node;
				{
					M_FontStyle *fs;
					M_Text *text; 
					M_Shape *shape = (M_Shape *)node;
					text = (M_Text *)gf_node_new(converter->bifs_sg, TAG_MPEG4_Text);
					shape->geometry = (GF_Node *)text;
					converter->bifs_text_node = shape->geometry;
					gf_node_register(shape->geometry, (GF_Node *)shape);
					
					fs = (M_FontStyle *)gf_node_new(converter->bifs_sg, TAG_MPEG4_XFontStyle);
					gf_node_register((GF_Node *)fs, (GF_Node*)text);
					text->fontStyle = (GF_Node *)fs;

					gf_sg_vrml_mf_alloc(&fs->family, GF_SG_VRML_MFSTRING, 1);				
					fs->family.vals[0] = strdup(converter->svg_props.font_family->value);				
					fs->size = converter->svg_props.font_size->value;

					shape->appearance = create_appearance(&converter->svg_props, converter->bifs_sg);
					gf_node_register(shape->appearance, (GF_Node *)shape);
				}
			}
			break;
		case TAG_SVG3_ellipse:
		case TAG_SVG3_circle:
			{
				Bool is_parent_set = 0;
				if (converter->all_atts.transform) {
					node = add_transform(converter, node);
					converter->bifs_parent = node;
					is_parent_set = 1;
				}
				if (converter->all_atts.cx || converter->all_atts.cy) {
					child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Transform2D);
					gf_node_register(child, node);
					gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
					{
						M_Transform2D *tr = (M_Transform2D *)child;
						if (converter->all_atts.cx) tr->translation.x = converter->all_atts.cx->value;
						if (converter->all_atts.cy) tr->translation.y = converter->all_atts.cy->value;
					}
					node = child;
					child = NULL;
					if (!is_parent_set) {
						converter->bifs_parent = node;
						is_parent_set = 1;
					}
				} 
				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Shape);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				if (!is_parent_set) converter->bifs_parent = node;
				{
					M_Shape *shape = (M_Shape *)node;
					if (tag == TAG_SVG3_ellipse) {
						M_Ellipse *e = (M_Ellipse *)gf_node_new(converter->bifs_sg, TAG_MPEG4_Ellipse);
						shape->geometry = (GF_Node *)e;
						e->radius.x = converter->all_atts.rx->value;
						e->radius.y = converter->all_atts.ry->value;
					} else {
						M_Circle *c = (M_Circle *)gf_node_new(converter->bifs_sg, TAG_MPEG4_Circle);
						shape->geometry = (GF_Node *)c;
						c->radius = converter->all_atts.r->value;
					}
					gf_node_register(shape->geometry, (GF_Node *)shape);
					
					shape->appearance = create_appearance(&converter->svg_props, converter->bifs_sg);
					gf_node_register(shape->appearance, (GF_Node *)shape);
				}
			}
			break;

		case TAG_SVG3_defs:
			{
				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Switch);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				{
					M_Switch *sw = (M_Switch *)node;
					sw->whichChoice = -1;
				}
				converter->bifs_parent = node;
			}
			break;
		case TAG_SVG3_solidColor:
			{
				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Shape);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				converter->bifs_parent = node;
			}
			break;
		default:
			{
				fprintf(stdout, "Warning: element %s not supported \n", gf_node_get_class_name((GF_Node *)elt));
				child = gf_node_new(converter->bifs_sg, TAG_MPEG4_Transform2D);
				gf_node_register(child, node);
				gf_node_list_add_child(&((GF_ParentNode*)node)->children, child);
				node = child;
				child = NULL;
				converter->bifs_parent = node;
			}
			break;
		}

		if (id_string) 
			gf_node_set_id(converter->bifs_parent, gf_node_get_id((GF_Node *)elt), gf_node_get_name((GF_Node *)elt));
	}
}

static void svg2bifs_node_end(void *sax_cbck, const char *name, const char *name_space)
{
	SVG2BIFS_Converter *converter = (SVG2BIFS_Converter *)sax_cbck;
	GF_Node *parent;

	SVGPropertiesPointers *backup_props = gf_node_get_private(converter->svg_parent);
	memcpy(&converter->svg_props, backup_props, sizeof(SVGPropertiesPointers));
//	free(backup_props);
	gf_node_set_private(converter->svg_parent, NULL);

	converter->bifs_parent = gf_node_get_parent(converter->bifs_parent, 0);
	parent = gf_node_get_parent(converter->svg_parent, 0);
	gf_node_unregister(converter->svg_parent, parent);	
	if (!parent) gf_sg_set_root_node(converter->svg_sg, NULL);
	converter->svg_parent = parent;
	converter->bifs_text_node = NULL;
}

static void svg2bifs_text_content(void *sax_cbck, const char *text_content, Bool is_cdata)
{
	SVG2BIFS_Converter *converter = (SVG2BIFS_Converter *)sax_cbck;
	if (converter->bifs_text_node) {
		M_Text *text = (M_Text *)converter->bifs_text_node;
		gf_sg_vrml_mf_alloc(&text->string, GF_SG_VRML_MFSTRING, 1);
		text->string.vals[0] = strdup(text_content);
	}
}

int main(int argc, char **argv)
{
	SVG2BIFS_Converter *converter;
	GF_SceneDumper *dump;
	char *tmp;

	gf_sys_init();

	GF_SAFEALLOC(converter, SVG2BIFS_Converter);

	converter->sax_parser = gf_xml_sax_new(svg2bifs_node_start, svg2bifs_node_end, svg2bifs_text_content, converter);
	
	converter->svg_sg = gf_sg_new();
	gf_svg_properties_init_pointers(&converter->svg_props);

	converter->bifs_sg = gf_sg_new();

	fprintf(stdout, "Parsing SVG File\n");
	gf_xml_sax_parse_file(converter->sax_parser, argv[1], NULL);

	fprintf(stdout, "Dumping BIFS scenegraph\n");
	tmp = strchr(argv[1], '.');
	tmp[0] = 0;
	dump = gf_sm_dumper_new(converter->bifs_sg, argv[1], ' ', GF_SM_DUMP_XMTA);
	tmp[0] = '.';

	gf_sm_dump_graph(dump, 1, 1);
	gf_sm_dumper_del(dump);

	gf_svg_properties_reset_pointers(&converter->svg_props);

	gf_sg_del(converter->svg_sg);
	gf_sg_del(converter->bifs_sg);

	gf_xml_sax_del(converter->sax_parser);
	
	free(converter);
}