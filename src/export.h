#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define export_ __declspec(dllexport)
#elif defined(__GNUC__) && ((__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3))
#define export_ __attribute__((visibility("default")))
#else
#define export_
#endif

#ifdef __cplusplus
}
#endif
