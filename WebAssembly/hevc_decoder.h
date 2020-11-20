void hevc_init(int va_count);
void hevc_destroy(int va_count);
double hevc_decode_nal_unit(int index, const unsigned char *data, size_t data_len, unsigned char *canvas, unsigned int _w, unsigned int _h, const unsigned char *alpha, unsigned char *bytes, const char *colourmap);

