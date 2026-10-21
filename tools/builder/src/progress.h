#ifndef TB_PROGRESS_H
#define TB_PROGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ProgressSink)(void* ctx, double fraction, const char* text);

void progress_start(ProgressSink sink, void* ctx);
void progress_terminal(void);
int progress_active(void);
void progress_phase(const char* key, const char* text);
void progress_items(int done, int total);
void progress_fraction(double sub);
void progress_stop(void);

#ifdef __cplusplus
}
#endif

#endif
