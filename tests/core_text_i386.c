struct rect { float x, y, width, height; };

extern void *CTFontCreateUIFontForLanguage(unsigned type, float size, void *language);
extern float CTFontGetSize(void *font);
extern unsigned char CTFontGetGlyphsForCharacters(void *font, const unsigned short *, unsigned short *, long);
extern struct rect CTFontGetBoundingBox(void *font);
extern struct rect CTFontGetBoundingRectsForGlyphs(void *font, unsigned orientation,
    const unsigned short *glyphs, struct rect *rects, long count);
extern void CFRelease(void *);

int main(void)
{
    void *font = CTFontCreateUIFontForLanguage(2, 18.0f, 0);
    if (!font || CTFontGetSize(font) != 18.0f) return 1;
    struct rect font_bounds = CTFontGetBoundingBox(font);
    if (font_bounds.width <= 0 || font_bounds.height <= 0) return 2;

    unsigned short characters[] = {'R', '3'}, glyphs[2];
    if (!CTFontGetGlyphsForCharacters(font, characters, glyphs, 2)) return 3;
    struct { struct rect rects[2]; unsigned sentinel; } out = {.sentinel = 0xdeadbeef};
    struct rect bounds = CTFontGetBoundingRectsForGlyphs(font, 0, glyphs, out.rects, 2);
    CFRelease(font);
    if (out.sentinel != 0xdeadbeef || out.rects[0].width <= 0 || out.rects[1].width <= 0) return 4;
    return bounds.width >= out.rects[0].width && bounds.width >= out.rects[1].width ? 0 : 5;
}
