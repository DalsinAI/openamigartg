/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * FreeType's module list for SDL2_ttf on AmigaOS 3.x: TrueType, OpenType
 * (CFF) and Type 1 fonts, hinted and anti-aliased. FreeType reads it through
 * FT_CONFIG_MODULES_H; the bitmap-font, SDF and SVG modules are left out. */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1cid_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
