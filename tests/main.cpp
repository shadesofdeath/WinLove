#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <objbase.h>

int main(int argc, char** argv) {
    // Render tests use WIC (COM). COM is deliberately never uninitialized: test fixtures keep
    // function-local statics holding COM objects that are destroyed after main returns, and
    // releasing them after CoUninitialize crashes. Process exit tears COM down safely.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    doctest::Context context(argc, argv);
    return context.run();
}
