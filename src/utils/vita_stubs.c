/**
 * Vita stubs for missing functions
 * These are required by various libraries but not available on Vita
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* Thread-safe stdio locking stubs - Vita is single-threaded for stdio anyway */
void flockfile(FILE *filehandle) {
    (void)filehandle;
    /* No-op on Vita */
}

void funlockfile(FILE *filehandle) {
    (void)filehandle;
    /* No-op on Vita */
}

/* SDL2 stub - DesktopPlatform uses this but we use PsvPlatform::openBrowser instead */
int SDL_OpenURL(const char *url) {
    (void)url;
    /* PSV uses sceAppUtilLaunchWebBrowser via PsvPlatform::openBrowser() */
    return -1;  /* Return error - actual implementation is in PsvPlatform */
}

/* FFmpeg deprecated function stubs
 * These were removed in newer FFmpeg but libmpv on Vita was built against older FFmpeg
 */

/* avcodec_close was deprecated in FFmpeg 4.x - replaced by avcodec_free_context */
int avcodec_close(void *avctx) {
    (void)avctx;
    /* Deprecated - contexts should be freed with avcodec_free_context */
    return 0;
}

/* av_stream_get_side_data was deprecated - use AVStream.side_data directly */
uint8_t *av_stream_get_side_data(const void *stream, int type, size_t *size) {
    (void)stream;
    (void)type;
    if (size) *size = 0;
    return NULL;
}

/* av_format_inject_global_side_data was deprecated with no replacement */
void av_format_inject_global_side_data(void *s) {
    (void)s;
    /* Deprecated - no-op */
}
