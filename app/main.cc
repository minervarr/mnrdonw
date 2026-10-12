#include "editor_app.hh"

int app_shell_main(int argc, char** argv) {
    EditorApp app;
    if (!app.create(argc, argv)) return 1;
    return app.run();
}
