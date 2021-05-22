/* DO NOT EDIT THIS FILE */
/* This file is autogenerated by the text-database code generator */

#ifndef APPLICATIONFONTPROVIDER_HPP
#define APPLICATIONFONTPROVIDER_HPP

#include <touchgfx/FontManager.hpp>

namespace touchgfx
{
    class FlashDataReader;
}

struct Typography
{
    static const touchgfx::FontId LABELS = 0;
};

struct TypographyFontIndex
{
    static const touchgfx::FontId LABELS = 0; // NotoSans_Regular_12_4bpp
    static const uint16_t NUMBER_OF_FONTS = 1;
};

class ApplicationFontProvider : public touchgfx::FontProvider
{
public:
    virtual touchgfx::Font* getFont(touchgfx::FontId typography);

    static void setFlashReader(touchgfx::FlashDataReader* /* flashReader */) { }
    static touchgfx::FlashDataReader* getFlashReader() { return 0; }
};

#endif // APPLICATIONFONTPROVIDER_HPP
