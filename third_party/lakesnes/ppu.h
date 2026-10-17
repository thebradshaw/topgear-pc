
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

// Top Gear port: lines can be drawn wider than 256 pixels (widescreen, ppu_full.c), up to
// this many pixels more on each side. The sprite line buffers have room for that: index
// them with PPU_OBJX(x).
#define PPU_WIDE_MAX 96
#define PPU_LINE_BUF (256 + 2 * PPU_WIDE_MAX)
#define PPU_OBJX(x) ((x) + PPU_WIDE_MAX)

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
  int16_t objGround[128];  // Top Gear port: the line each entry's object stands on, or PPU_NO_GROUND
  uint8_t objFlag[128];    // Top Gear port: PPU_OBJ_GHOST: drawn see-through; PPU_OBJ_LEFT
  // Top Gear port, high frame rate: which object each entry belongs to (0: none) and that
  // object's position, so in-between frames can move it; and the line's road depth (track
  // units x 8 ahead of the camera, PPU_NO_DEPTH off the road) and camera position.
  uint16_t objIdent[128];
  int16_t objAnchorX[128], objAnchorY[128];
  int16_t lineDepth;
  uint16_t lineCam;
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
  const int16_t* objGrounds;  // by low RAM address of an OAM entry (ppu_tagObj)
  const uint8_t* objFlags;    // the same for objFlag
  const uint16_t* objIdents;  // the same for objIdent, objAnchorX and objAnchorY
  const int16_t* objAnchorXs;
  const int16_t* objAnchorYs;
  const int16_t* lineDepths;  // by low RAM address of a line's BG3 scroll entry
  const uint16_t* lineCams;
  bool interp;                // keep the last two race frames for ppu_interpCompose
  uint8_t* raceFrame;
  struct PpuFullView* full;
  uint8_t fullArmed;   // this frame's flags, latched at the last vblank
  bool fullComposing;
  bool fullSkipDraw;   // the last race frame was redrawn: this one's lines need not be drawn as they come
  int16_t objShift[128];      // while composing: how far each entry moves down
  bool hudMapTop;              // full view: the map stays at the top left
  bool hudSpeedBottom;         // full view: the speedometer and timer go to the bottom right
  float roadScale;            // full view: how much taller the road is drawn (0: 1.3, horizon low as in the original)
  int wideMargin;             // widescreen: extra pixels on each side (0: off)
  uint32_t* wideBuffer;       // the wide picture, XRGB, (256 + 2 * wideMargin) x 224
  // The line being drawn: pixels lineLeft to lineRight - 1, into lineOut when set (else
  // pixelBuffer); hudMargin moves the HUD's sides out by that much.
  int16_t lineLeft, lineRight, hudMargin;
  // Widescreen and full view move the HUD's parts (ppu_full.c). When hudLine is set, BG1's
  // pixel at x on this line is the game's pixel hudLine[PPU_OBJX(x)] (source row << 9 |
  // column; -1: none), scrolled as that row was (hudRowH/V); HUD sprites move objHudDx.
  const int32_t* hudLine;
  const uint16_t* hudRowH;
  const uint16_t* hudRowV;
  int16_t objHudDx[128];
  uint32_t* lineOut;
  // Sprite line buffers (kept out of the line state ppu_full.c copies).
  float objAlpha[PPU_LINE_BUF], objR[PPU_LINE_BUF], objG[PPU_LINE_BUF], objB[PPU_LINE_BUF];  // see-through object pixels
  uint8_t objPixelBuffer[PPU_LINE_BUF];
  uint8_t objPriorityBuffer[PPU_LINE_BUF];
};

#define PPU_RACE_FRAME 1
#define PPU_FULL_VIEW 2
#define PPU_SIDE_BY_SIDE 4  // 2 players: the two views side by side, each full height
#define PPU_PIT_TOP 8       // the top view shows the pit stop scene (widescreen: nothing past its sides)
#define PPU_PIT_BOTTOM 16   // the same for the bottom view

#define PPU_NO_GROUND (-32768)
#define PPU_NO_DEPTH (-32767)  // a line's scroll entry off the road
#define PPU_NOT_ENTRY (-32768) // not the start of a scroll entry
#define PPU_OBJ_GHOST 1
#define PPU_OBJ_BLANK 4  // an entry the game parks past the right edge to fill a line's sprite limit
#define PPU_OBJ_LEFT 2   // the entry's object is left of centre: x past 255 wraps to the left
#define PPU_GHOST_ALPHA 0.45f

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
void ppu_fullCapture(Ppu* ppu, int line);
void ppu_fullCompose(Ppu* ppu);
void ppu_tagLine(Ppu* ppu, uint8_t bank, uint16_t adr);
// High frame rate: redraws the last race frame as it would look t (0-1) of a frame after
// the one before it. False when there are not two race frames to go between.
bool ppu_interpCompose(Ppu* ppu, float t);
int ppu_fullMapRow(Ppu* ppu, int row);
int ppu_fullViewOff(Ppu* ppu);
int ppu_objSize(Ppu* ppu, int i);
bool ppu_bgOpaque(Ppu* ppu, int layer, int x, int line);

#endif
