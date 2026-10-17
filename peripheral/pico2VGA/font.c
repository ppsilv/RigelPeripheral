#include "font.h"

static font_t font_8x11_info = {
    .name = "Font 8x11",
    .height = 11,
    .width = 8,
    .size = 1,
    .data = font_8x11
};


// Instância da fonte 8x12
static font_t font_8x12_info = {
    .name = "Font 8x12",
    .height = 12,
    .width = 8,
    .size = 1,            
    .data = font_8x12
};


// Instância da fonte 8x14
static font_t font_8x14_info = {
    .name = "Font 8x14",
    .height = 14,
    .width = 8,
    .size = 1,            
    .data = font_8x14
};
/*
// Instância da fonte 8x16
static font_t font_8x16_info = {
    .name = "Font 8x16",
    .height = 16,
    .width = 8,
    .size = 1,            
    .data = font_8x16
};
*/
// Instância da fonte 8x16
static font_t font_8x16_2_info = {
    .name = "Font 8x16_2",
    .height = 16,
    .width = 8,
    .size = 1,            
    .data = font_8x16_2
};
static font_t font_8x16_font_sperry_info = {
    .name = "Font 8x16_font_sperry",
    .height = 16,
    .width = 8,
    .size = 1,
    .data = font_sperry
};

static font_t font_8x16_font_acm_info = {
    .name = "Font 8x16_font_acm",
    .height = 16,
    .width = 8,
    .size = 1,
    .data = font_acm
};

static font_t font_8x16_font_sweet16_info = {
    .name = "Font 8x16_font_sweet16",
    .height = 16,
    .width = 8,
    .size = 1,
    .data = font_sweet16
};
static font_t font_8x16_font_toshiba_info = {
    .name = "Font 8x16_font_toshiba",
    .height = 16,
    .width = 8,
    .size = 1,
    .data = font_toshiba
};
static font_t font_8x16_font_verite_info = {
    .name = "Font 8x16_font_verite",
    .height = 16,
    .width = 8,
    .size = 1,
    .data = font_verite
};

// Instância da fonte 8x18 (para seus títulos legais)
static font_t font_8x18_info = {
    .name = "Font 8x18",
    .height = 18,
    .width = 8,
    .size = 1,            
    .data = font_8x18
};

font_t * set_font(uint8_t fonte){
    switch(fonte){
        case FONTE_8X11:
            return &font_8x11_info;
            break;
        case FONTE_8X12:
             return &font_8x12_info;
             break;
        case FONTE_8X14:
             return &font_8x14_info;
             break;
        case FONTE_8X16:
       //      return &font_8x16_info;
             break;
        case FONTE_8X16_2:
             return &font_8x16_2_info;
             break;
        case FONTE_8X18:
             return &font_8x18_info;
             break;
        case FONTE_8X16_VERITE:
            return &font_8x16_verite_info;
            break;
        case FONTE_8X16_TOSHIBA:
            return &font_8x16_toshiba_info;
            break;
        case FONTE_8X16_ACM:
            return &font_8x16_acm_info;
            break;
        case FONTE_8X16_SPERRY:
            return &font_8x16_sperry_info;
            break;
        case FONTE_8X16_SWEET:
            return &font_8x16_sweet_info;
            break;
        default:
            return 0;             
    }
}
