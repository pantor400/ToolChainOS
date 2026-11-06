/* Host-only fixtures: no i8042 or image modules. Actual gallery and mouse
 * behavior are verified by test_gui.py/test_raton.py under QEMU. */
#include "images.h"
void mouse_poll(void) {}
void images_init(void) {}
int images_count(void) { return 0; }
const char *images_name(int index) { (void)index; return ""; }
const char *images_extension(int index) { (void)index; return ".jpg"; }
int images_width(int index) { (void)index; return 0; }
int images_height(int index) { (void)index; return 0; }
