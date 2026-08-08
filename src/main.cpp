#include "ui/Application.h"
#include <QApplication>
#include <cstdlib>

int main(int argc, char **argv) {
    qputenv("QT_FFMPEG_PROTOCOL_WHITELIST", "file,crypto,data,https,tls,tcp,http,concat");
    QApplication qapp(argc, argv);
    Application app;
    return app.run(argc, argv);
}
