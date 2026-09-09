/*
 * Gazette_icon.r - Finder icon for Gazette.
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_icon.py --out Resources/Gazette_icon.r
 *
 * A folded newspaper: a masthead band, a lead picture and columns of text,
 * with a second sheet showing behind. Written as raw data blocks rather than
 * Rez icon templates because Rez runs against whichever RIncludes the
 * toolchain has linked, and a resource file with no includes does not care
 * which -- the same reasoning Gateway's icon file records.
 *
 * BNDL and FREF tie the icon family to the 'Gzt9' creator so the Finder uses
 * it. If a freshly built copy shows a generic application icon, the desktop
 * database has not caught up: rebuild it by holding Command-Option through
 * startup, or move the application to another folder and back.
 */

data 'ICN#' (128, "Gazette", purgeable) {
	$"00 00 00 00 00 00 00 00 00 FF FF FC 00 80 00 04"
	$"00 80 00 04 3F FF FF C4 20 00 00 44 2F FF FF 44"
	$"2F FF FF 44 2F FF FF 44 2F FF FF 44 2F FF FF 44"
	$"20 00 00 44 2F FF FF 44 20 00 00 44 20 00 00 44"
	$"2F FB FF 44 28 78 00 44 28 7B FF 44 28 78 00 44"
	$"2F FB FF 44 2F F8 00 44 2F FB FF 44 2F F8 00 44"
	$"20 00 00 44 2F FF FF 44 20 00 00 7C 2F FF F0 40"
	$"20 00 00 40 3F FF FF C0 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF FF FC 00 FF FF FC"
	$"00 FF FF FC 3F FF FF FC 3F FF FF FC 3F FF FF FC"
	$"3F FF FF FC 3F FF FF FC 3F FF FF FC 3F FF FF FC"
	$"3F FF FF FC 3F FF FF FC 3F FF FF FC 3F FF FF FC"
	$"3F FF FF FC 3F FF FF FC 3F FF FF FC 3F FF FF FC"
	$"3F FF FF FC 3F FF FF FC 3F FF FF FC 3F FF FF FC"
	$"3F FF FF FC 3F FF FF FC 3F FF FF FC 3F FF FF C0"
	$"3F FF FF C0 3F FF FF C0 00 00 00 00 00 00 00 00"
};

data 'icl8' (128, "Gazette", purgeable) {
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF FF FF FF FF FF FF 00 00"
	$"00 00 00 00 00 00 00 00 FF 2B 2B 2B 2B 2B 2B 2B"
	$"2B 2B 2B 2B 2B 2B 2B 2B 2B 2B 2B 2B 2B FF 00 00"
	$"00 00 00 00 00 00 00 00 FF 2B 2B 2B 2B 2B 2B 2B"
	$"2B 2B 2B 2B 2B 2B 2B 2B 2B 2B 2B 2B 2B FF 00 00"
	$"00 00 FF FF FF FF FF FF FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF FF FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF FF FF FF FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF FF FF FF FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF FF FF FF FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF FF FF FF FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF FF FF FF FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 81 81 81 81 81 81 81 81 81 81 81 81"
	$"81 81 81 81 81 81 81 81 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF FF FF FF FF FF FF FF FF 00 81 81"
	$"81 81 81 81 81 81 81 81 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF 56 56 56 56 81 81 81 FF 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF 56 56 56 56 81 81 81 FF 00 81 81"
	$"81 81 81 81 81 81 81 81 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF 56 56 56 56 81 81 81 FF 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF 81 81 81 81 81 81 81 FF 00 81 81"
	$"81 81 81 81 81 81 81 81 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF 81 81 81 81 81 81 81 FF 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF 81 81 81 81 81 81 81 FF 00 81 81"
	$"81 81 81 81 81 81 81 81 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 FF FF FF FF FF FF FF FF FF 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 2B 2B 2B FF 00 00"
	$"00 00 FF 00 81 81 81 81 81 81 81 81 81 81 81 81"
	$"81 81 81 81 81 81 81 81 00 FF 56 56 56 FF 00 00"
	$"00 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF FF FF FF FF 00 00"
	$"00 00 FF 00 81 81 81 81 81 81 81 81 81 81 81 81"
	$"81 81 81 81 00 00 00 00 00 FF 00 00 00 00 00 00"
	$"00 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 FF 00 00 00 00 00 00"
	$"00 00 FF FF FF FF FF FF FF FF FF FF FF FF FF FF"
	$"FF FF FF FF FF FF FF FF FF FF 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
};

data 'ics#' (128, "Gazette", purgeable) {
	$"00 00 07 FE 04 02 04 02 7F F2 40 12 7F F2 7F F2"
	$"40 12 7F F2 40 12 7F FE 40 10 7F 10 7F F0 00 00"
	$"00 00 07 FE 07 FE 07 FE 7F FE 7F FE 7F FE 7F FE"
	$"7F FE 7F FE 7F FE 7F FE 7F F0 7F F0 7F F0 00 00"
};

data 'ics8' (128, "Gazette", purgeable) {
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	$"00 00 00 00 00 FF FF FF FF FF FF FF FF FF FF 00"
	$"00 00 00 00 00 FF 2B 2B 2B 2B 2B 2B 2B 2B FF 00"
	$"00 00 00 00 00 FF 2B 2B 2B 2B 2B 2B 2B 2B FF 00"
	$"00 FF FF FF FF FF FF FF FF FF FF FF 2B 2B FF 00"
	$"00 FF 00 00 00 00 00 00 00 00 00 FF 2B 2B FF 00"
	$"00 FF FF FF FF FF FF FF FF FF FF FF 2B 2B FF 00"
	$"00 FF FF FF FF FF FF FF FF FF FF FF 2B 2B FF 00"
	$"00 FF 00 00 00 00 00 00 00 00 00 FF 2B 2B FF 00"
	$"00 FF 81 81 81 81 81 81 81 81 81 FF 2B 2B FF 00"
	$"00 FF 00 00 00 00 00 00 00 00 00 FF 2B 2B FF 00"
	$"00 FF 81 81 81 81 81 81 81 81 81 FF FF FF FF 00"
	$"00 FF 00 00 00 00 00 00 00 00 00 FF 00 00 00 00"
	$"00 FF 81 81 81 81 81 81 00 00 00 FF 00 00 00 00"
	$"00 FF FF FF FF FF FF FF FF FF FF FF 00 00 00 00"
	$"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
};

data 'FREF' (128, "Gazette", purgeable) {
	$"4150 504C"        /* type APPL          */
	$"0000"             /* local icon list ID */
	$"00"               /* empty name         */
};

data 'BNDL' (128, "Gazette", purgeable) {
	$"477A 7439"        /* signature Gzt9     */
	$"0000"             /* version            */
	$"0001"             /* two type entries   */
	$"4943 4E23"        /* ICN#               */
	$"0000"             /* one mapping        */
	$"0000 0080"        /* local 0 -> 128     */
	$"4652 4546"        /* FREF               */
	$"0000"             /* one mapping        */
	$"0000 0080"        /* local 0 -> 128     */
};

data 'Gzt9' (0, "Gazette", purgeable) {
	$"0B" "Gazette 0.1"
};
