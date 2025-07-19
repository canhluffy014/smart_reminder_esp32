# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(SRCS main.c rgb_led.c wifi_app.c http_server.c ldr_gl5537.c
                       INCLUDE_DIRS "."
                       EMBED_FILES Webpage/app.css Webpage/app.js Webpage/favicon.ico Webpage/index.html Webpage/jquery-3.3.1.min.js)


