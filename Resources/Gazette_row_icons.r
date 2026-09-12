/*
 * Gazette_row_icons.r - the sidebar's own small icons.
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_row_icons.py --out Resources/Gazette_row_icons.r
 *
 * 16x16 families only: these are drawn in list rows and nowhere else, so
 * there is no 32x32 member to carry. GetIconSuite reads them by ID out of the
 * application's own resource fork and PlotIconSuite draws them, which is the
 * one path that does not need an Icon Services registration -- the system
 * icons beside them in the sidebar have no resource IDs to read, and these
 * have no reason to be registered.
 *
 * Written as raw data blocks rather than Rez icon templates for the reason
 * Gazette_icon.r records: Rez runs against whichever RIncludes the toolchain
 * has linked, and a resource file with no includes does not care which.
 *
 * IDs are the kGazetteIcon* constants in src/ui/platinum_window.c.
 */

data 'ics#' (128, "Today", purgeable) {
	$"01 80 01 80 20 04 00 00 07 E0 08 10 08 10 C8 13"
	$"C8 13 08 10 08 10 07 E0 00 00 20 04 01 80 01 80"
	$"01 80 01 80 20 04 00 00 07 E0 0F F0 0F F0 CF F3"
	$"CF F3 0F F0 0F F0 07 E0 00 00 20 04 01 80 01 80"
};

data 'ics8' (128, "Today", purgeable) {
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
	$"00 00 3B 00 00 00 00 00 00 00 00 00 00 3B 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 3B 3B 3B 3B 3B 3B 00 00 00 00 00"
	$"00 00 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 00 00"
	$"00 00 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 00 00"
	$"3B 3B 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 3B 3B"
	$"3B 3B 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 3B 3B"
	$"00 00 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 00 00"
	$"00 00 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 00 00"
	$"00 00 00 00 00 3B 3B 3B 3B 3B 3B 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 3B 00 00 00 00 00 00 00 00 00 00 3B 00 00"
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
};

data 'ics#' (129, "All Unread", purgeable) {
	$"00 00 00 00 07 E0 1F F8 1F F8 3F FC 3F FC 3F FC"
	$"3F FC 3F FC 3F FC 1F F8 1F F8 07 E0 00 00 00 00"
	$"00 00 00 00 07 E0 1F F8 1F F8 3F FC 3F FC 3F FC"
	$"3F FC 3F FC 3F FC 1F F8 1F F8 07 E0 00 00 00 00"
};

data 'ics8' (129, "All Unread", purgeable) {
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 CE CE CE CE CE CE 00 00 00 00 00"
	$"00 00 00 CE CE A2 A2 A2 A2 A2 A2 CE CE 00 00 00"
	$"00 00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00 00"
	$"00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00"
	$"00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00"
	$"00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00"
	$"00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00"
	$"00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00"
	$"00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00"
	$"00 00 00 CE A2 A2 A2 A2 A2 A2 A2 A2 CE 00 00 00"
	$"00 00 00 CE CE A2 A2 A2 A2 A2 A2 CE CE 00 00 00"
	$"00 00 00 00 00 CE CE CE CE CE CE 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
};

data 'ics#' (130, "Starred", purgeable) {
	$"00 00 00 00 01 80 01 80 02 40 04 20 78 1E 20 04"
	$"10 08 08 10 08 10 09 90 0E 70 08 10 00 00 00 00"
	$"00 00 00 00 01 80 01 80 03 C0 07 E0 7F FE 3F FC"
	$"1F F8 0F F0 0F F0 0F F0 0E 70 08 10 00 00 00 00"
};

data 'ics8' (130, "Starred", purgeable) {
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 3B 0B 0B 3B 00 00 00 00 00 00"
	$"00 00 00 00 00 3B 0B 0B 0B 0B 3B 00 00 00 00 00"
	$"00 3B 3B 3B 3B 0B 0B 0B 0B 0B 0B 3B 3B 3B 3B 00"
	$"00 00 3B 0B 0B 0B 0B 0B 0B 0B 0B 0B 0B 3B 00 00"
	$"00 00 00 3B 0B 0B 0B 0B 0B 0B 0B 0B 3B 00 00 00"
	$"00 00 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 00 00"
	$"00 00 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 00 00"
	$"00 00 00 00 3B 0B 0B 3B 3B 0B 0B 3B 00 00 00 00"
	$"00 00 00 00 3B 3B 3B 00 00 3B 3B 3B 00 00 00 00"
	$"00 00 00 00 3B 00 00 00 00 00 00 3B 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
};

data 'ics#' (131, "Starred Article", purgeable) {
	$"00 00 00 00 00 00 00 00 01 80 01 80 0E 70 10 08"
	$"08 10 04 20 04 20 07 E0 04 20 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 01 80 01 80 0F F0 1F F8"
	$"0F F0 07 E0 07 E0 07 E0 04 20 00 00 00 00 00 00"
};

data 'ics8' (131, "Starred Article", purgeable) {
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 3B 3B 00 00 00 00 00 00 00"
	$"00 00 00 00 3B 3B 3B 0B 0B 3B 3B 3B 00 00 00 00"
	$"00 00 00 3B 0B 0B 0B 0B 0B 0B 0B 0B 3B 00 00 00"
	$"00 00 00 00 3B 0B 0B 0B 0B 0B 0B 3B 00 00 00 00"
	$"00 00 00 00 00 3B 0B 0B 0B 0B 3B 00 00 00 00 00"
	$"00 00 00 00 00 3B 0B 0B 0B 0B 3B 00 00 00 00 00"
	$"00 00 00 00 00 3B 3B 3B 3B 3B 3B 00 00 00 00 00"
	$"00 00 00 00 00 3B 00 00 00 00 3B 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
};
