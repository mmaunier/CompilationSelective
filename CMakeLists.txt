cmake_minimum_required(VERSION 3.16)
project(CompilationSelective 
    VERSION 0.1.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(${PROJECT_NAME}
    main.cpp
    src/latexparser.cpp
    src/latexmodel.cpp
    src/lastfilehelper.cpp
    src/processrunner.cpp
    src/latexassembler.cpp
    resources.qrc
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Ajouter la version dans le code
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    APP_VERSION="${PROJECT_VERSION}"
)