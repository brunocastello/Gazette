# stb_image

`stb_image.h` v2.30 from https://github.com/nothings/stb, commit
`2c980bb59875b0d32144a71867fbdebb2f77cd20`, unmodified. Public domain
(or MIT, at your choice) -- the licence text is at the foot of the file.

Used by the Windows build only, to decode the photographs in an article
(`src/win/gazette_win_image.c`, which sets the trimming defines: JPEG, PNG
and GIF, no stdio, no SIMD, no thread-locals). Suggested by roytam1,
2026-09-26. The Mac build draws its photographs with QuickTime and does
not compile this.

To update: replace the file with a newer release and record the commit here.
