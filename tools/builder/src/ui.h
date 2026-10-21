#ifndef TB_UI_H
#define TB_UI_H

typedef enum { CRACK_QUIT, CRACK_BUY, CRACK_CONTINUE } CrackChoice;

int ui_native_available(void);
void ui_use_native(int native);
int ui_is_native(void);
int ui_yes_no(const char* title, const char* text, const char* yes, const char* no);
int ui_choose(const char* title, const char* text, const char* const* items, int count);
char* ui_pick_folder(const char* title, const char* prompt);
void ui_message(const char* title, const char* text, int error);
CrackChoice ui_crack_warning(const char* title, const char* text);
void ui_open_url(const char* url);
int ui_stdin_is_terminal(void);
int ui_progress_open(const char* title, const char* text);
void ui_progress_update(double fraction, const char* text);
void ui_progress_close(void);

#endif
