/* The desktop's background (Veranda Look 2, step C: DECISIONS V78), for
   the shell and Preferences.

   WALL_CFG, a plain file of WALL_CFGLEN bytes in the system's data folder
   (VR_DATADIR, /VERANDA/.SYSTEM), written by Preferences (Desktop page)
   and by the shell (options > desktop pattern), read by the shell at its
   start and when a desk accessory closes:
     0-1   "WP"
     2     1 (the version)
     3     the kind: WALL_PICTURE, WALL_PLAIN (the theme's desktop colour),
           WALL_PATTERN (a GEOS pattern on it, the classic look)
     4     the pattern (0-7: the shell's eight)
     8-31  the picture: its GEOS name (a wallpaper file, NAME.cvt;
           0-terminated)
     32-63 its folder (0-terminated; VR_WALLDIR, /VERANDA/WALLPAPERS)
   No file: the picture WALL_DEFAULT in VR_WALLDIR. A wallpaper is a GEOS
   document of class WALL_CLASS made by tools/mkwall.py
   (docs/APPFORMAT.md, "Wallpapers"); a missing one, the Classic theme
   (no colours) or a screen 320 wide give the pattern instead. Dark mode
   takes the file's dark variant. */
#ifndef WALLCFG_H
#define WALLCFG_H

#define WALL_CFG      "WALLPAPER.CFG"
#define WALL_CFGLEN   64
#define WALL_KIND     3
#define WALL_PAT      4
#define WALL_NAME     8
#define WALL_NAMELEN  24
#define WALL_DIR      32
#define WALL_DIRLEN   32
#define WALL_PICTURE  0
#define WALL_PLAIN    1
#define WALL_PATTERN  2
#define WALL_DEFAULT  "Dusk"
#define WALL_CLASS    "Wallpaper"

/* the file's data (from byte 508 of the CVT file) */
#define WALL_MAGIC    "VWAL"
#define WALL_HEAD     16            /* then the variants: light, dark */
#define WALL_VARIANT  43208u        /* 8 palette, 4800 colours, 38400 bitmap */
#define WALL_COLOURS  4800u
#define WALL_BITMAP   38400u

#endif
