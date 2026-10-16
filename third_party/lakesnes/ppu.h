
#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Ppu Ppu;

#include "snes.h"
#include "statehandler.h"

typedef struct BgLayer {
  uint16_t hScroll;
  uint16_t vScroll;
  bool tilemapWider;
  bool tilemapHigher;
  uint16_t tilemapAdr;
  uint16_t tileAdr;
  bool bigTiles;
  bool mosaicEnabled;
} BgLayer;

typedef struct Layer {
  bool mainScreenEnabled;
  bool subScreenEnabled;
  bool mainScreenWindowed;
  bool subScreenWindowed;
} Layer;

// Top Gear port enhancement: an object drawn smoothly scaled from its own art, filtered,
// in place of the OAM entries tagged with it (ppu_smooth.c).
// Top Gear port: lines can be drawn wider than 256 pixels (widescreen, ppu_full.c), up to
// this many pixels more on each side. The sprite line buffers have room for that: index
// them with PPU_OBJX(x).
#define PPU_WIDE_MAX 96
#define PPU_LINE_BUF (256 + 2 * PPU_WIDE_MAX)
#define PPU_OBJX(x) ((x) + PPU_WIDE_MAX)

#define PPU_OBJ_PARTS 32
typedef struct PpuObjPart {
  int16_t dx, dy;  // from the origin, in the art's pixels
  uint16_t attr;   // tile, palette, priority and flips, as in OAM
  uint8_t big;     // the large sprite size
} PpuObjPart;

typedef struct PpuObjScale {
  float ax, ay, f;              // the art's origin on screen (OAM coordinates), and its scale
  int16_t clipTop, clipBottom;  // the lines it may cover
  uint8_t count;
  PpuObjPart parts[PPU_OBJ_PARTS];  // the first on top
} PpuObjScale;

typedef struct PpuObjBitmap {
  uint16_t tag;
  uint32_t frame;
  int x0, y0, w, h;
  float a[96 * 96], r[96 * 96], g[96 * 96], b[96 * 96];
  uint8_t index[96 * 96];
} PpuObjBitmap;

typedef struct WindowLayer {
  bool window1enabled;
  bool window2enabled;
  bool window1inversed;
  bool window2inversed;
  uint8_t maskLogic;
} WindowLayer;

struct Ppu {
  Snes* snes;
  // vram access
  uint16_t vram[0x8000];
  uint16_t vramPointer;
  bool vramIncrementOnHigh;
  uint16_t vramIncrement;
  uint8_t vramRemapMode;
  uint16_t vramReadBuffer;
  // cgram access
  uint16_t cgram[0x100];
  uint8_t cgramPointer;
  bool cgramSecondWrite;
  uint8_t cgramBuffer;
  // oam access
  uint16_t oam[0x100];
  uint8_t highOam[0x20];
  uint8_t oamAdr;
  uint8_t oamAdrWritten;
  bool oamInHigh;
  bool oamInHighWritten;
  bool oamSecondWrite;
  uint8_t oamBuffer;
  // object/sprites
  bool objPriority;
  uint16_t objTileAdr1;
  uint16_t objTileAdr2;
  uint8_t objSize;
  // Top Gear port: when objScaleTags is set, each OAM entry DMA'd from low RAM takes the
  // tag at its source address (0: drawn as usual); entries with a tag show the object
  // objScales[tag] instead, drawn at the first of them
  const uint16_t* objScaleTags;
  const PpuObjScale* objScales;
  uint16_t objTag[128];
  int16_t objGround[128];  // Top Gear port: the line each entry's object stands on, or PPU_NO_GROUND
  PpuObjBitmap* objBitmaps;  // a small cache, allocated when first needed
  bool timeOver;
  bool rangeOver;
  bool objInterlace;
  // background layers
  BgLayer bgLayer[4];
  uint8_t scrollPrev;
  uint8_t scrollPrev2;
  uint8_t mosaicSize;
  uint8_t mosaicStartLine;
  // layers
  Layer layer[5];
  // mode 7
  int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
  uint8_t m7prev;
  bool m7largeField;
  bool m7charFill;
  bool m7xFlip;
  bool m7yFlip;
  bool m7extBg;
  // mode 7 internal
  int32_t m7startX;
  int32_t m7startY;
  // windows
  WindowLayer windowLayer[6];
  uint8_t window1left;
  uint8_t window1right;
  uint8_t window2left;
  uint8_t window2right;
  // color math
  uint8_t clipMode;
  uint8_t preventMathMode;
  bool addSubscreen;
  bool subtractColor;
  bool halfColor;
  bool mathEnabled[6];
  uint8_t fixedColorR;
  uint8_t fixedColorG;
  uint8_t fixedColorB;
  // settings
  bool forcedBlank;
  uint8_t brightness;
  uint8_t mode;
  bool bg3priority;
  bool evenFrame;
  bool pseudoHires;
  bool overscan;
  bool frameOverscan; // if we are overscanning this frame (determined at 0,225)
  bool interlace;
  bool frameInterlace; // if we are interlacing this frame (determined at start vblank)
  bool directColor;
  // latching
  uint16_t hCount;
  uint16_t vCount;
  bool hCountSecond;
  bool vCountSecond;
  bool countersLatched;
  uint8_t ppu1openBus;
  uint8_t ppu2openBus;
  // pixel buffer (xbgr)
  // times 2 for even and odd frame
  uint8_t pixelBuffer[512 * 4 * 239 * 2];
  uint8_t pixelOutputFormat;
  // Top Gear port: race frames redrawn wider (widescreen) or with the 1-player view over the
  // whole screen (ppu_full.c). During a frame the game sets *raceFrame to PPU_RACE_FRAME
  // (and PPU_FULL_VIEW) when the next frame shows a race; the PPU then keeps the state of
  // each line as it is drawn and redraws the screen from them at the start of vblank.
  const int16_t* objGrounds;  // by low RAM address of an OAM entry, like objScaleTags
  uint8_t* raceFrame;
  struct PpuFullView* full;
  uint8_t fullArmed;   // this frame's flags, latched at the last vblank
  bool fullComposing;
  int16_t objShift[128];      // while composing: how far each entry moves down
  int wideMargin;             // widescreen: extra pixels on each side (0: off)
  uint32_t* wideBuffer;       // the wide picture, XRGB, (256 + 2 * wideMargin) x 224
  // The line being drawn: pixels lineLeft to lineRight - 1, into lineOut when set (else
  // pixelBuffer); hudMargin moves the HUD's sides out by that much.
  int16_t lineLeft, lineRight, hudMargin;
  bool hudClamp;  // the HUD covers the whole line: carry its edges on instead
  uint32_t* lineOut;
  // Sprite line buffers (kept out of the line state ppu_full.c copies).
  float objAlpha[PPU_LINE_BUF], objR[PPU_LINE_BUF], objG[PPU_LINE_BUF], objB[PPU_LINE_BUF];  // filtered object pixels
  uint8_t objPixelBuffer[PPU_LINE_BUF];
  uint8_t objPriorityBuffer[PPU_LINE_BUF];
};

#define PPU_RACE_FRAME 1
#define PPU_FULL_VIEW 2
#define PPU_SIDE_BY_SIDE 4  // 2 players: the two views side by side, each full height

#define PPU_NO_GROUND (-32768)

enum { ppu_pixelOutputFormatXBGR = 0, ppu_pixelOutputFormatBGRX = 1 };

Ppu* ppu_init(Snes* snes);
void ppu_free(Ppu* ppu);
void ppu_reset(Ppu* ppu);
void ppu_handleState(Ppu* ppu, StateHandler* sh);
bool ppu_checkOverscan(Ppu* ppu);
void ppu_handleVblank(Ppu* ppu);
void ppu_handleFrameStart(Ppu* ppu);
void ppu_runLine(Ppu* ppu, int line);
uint8_t ppu_read(Ppu* ppu, uint8_t adr);
void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val);
void ppu_putPixels(Ppu* ppu, uint8_t* pixels);
void ppu_setPixelOutputFormat(Ppu* ppu, int pixelOutputFormat);

void ppu_tagObj(Ppu* ppu, uint8_t bank, uint16_t adr);
int ppu_smoothOnLine(Ppu* ppu, uint8_t index, int line);
void ppu_smoothDrawLine(Ppu* ppu, uint8_t index, int line);
void ppu_fullCapture(Ppu* ppu, int line);
void ppu_fullCompose(Ppu* ppu);
int ppu_fullMapRow(Ppu* ppu, int row);
int ppu_fullViewOff(Ppu* ppu);
#define PPU_NO_HUD (-32768)
int ppu_hudSource(Ppu* ppu, int x);
int ppu_hudShift(Ppu* ppu, int x);
bool ppu_bgOpaque(Ppu* ppu, int layer, int x, int line);

#endif
