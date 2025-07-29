#include <SDL3/SDL_render.h>
#include <stddef.h>
#include "button.h"
#include "common.h"
#include "label.h"

extern bool running_dialog_box;
extern bool dialog_box_statment;
extern struct LE_Label dialog_box_main_label, dialog_box_true_label, dialog_box_false_label;
extern struct SDL_Texture *dialog_box_texture;
extern struct LE_RenderElement dialog_box_main_element, dialog_box_true_element, dialog_box_false_element;
extern struct LE_Button dialog_box_true_button, dialog_box_false_button;

bool InitDialogBox(SDL_Renderer *pRenderer, const char *str, const char *true_str, const char *false_str);
bool RenderDialogBox(void);
