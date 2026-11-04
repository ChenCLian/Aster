QT = core gui widgets sql printsupport network
TEMPLATE = lib
TARGET = aster_desktop_ui
CONFIG += staticlib c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
INCLUDEPATH += ../src
DESTDIR = $$OUT_PWD/../lib
win32-msvc:QMAKE_CXXFLAGS += /utf-8 /bigobj

SOURCES += \
    ../src/third_party/qcustomplot/qcustomplot.cpp \
    ../src/ui/app/main_window.cpp \
    ../src/ui/common/app_settings.cpp \
    ../src/ui/common/presentation.cpp \
    ../src/ui/panels/parameter_editor.cpp \
    ../src/ui/panels/collapsible_panel.cpp \
    ../src/ui/panels/source_panel.cpp \
    ../src/ui/panels/task_catalog.cpp \
    ../src/ui/results/plot_view.cpp \
    ../src/ui/results/results_page.cpp \
    ../src/ui/services/result_loader.cpp \
    ../src/ui/services/run_controller.cpp \
    ../src/ui/workflow/workflow_canvas.cpp \
    ../src/ui/workflow/workflow_document.cpp

HEADERS += \
    ../src/third_party/qcustomplot/qcustomplot.h \
    ../src/ui/app/main_window.h \
    ../src/ui/common/app_settings.h \
    ../src/ui/common/presentation.h \
    ../src/ui/panels/collapsible_panel.h \
    ../src/ui/panels/parameter_editor.h \
    ../src/ui/panels/source_panel.h \
    ../src/ui/panels/task_catalog.h \
    ../src/ui/results/plot_view.h \
    ../src/ui/results/results_page.h \
    ../src/ui/services/result_loader.h \
    ../src/ui/services/run_controller.h \
    ../src/ui/workflow/workflow_canvas.h \
    ../src/ui/workflow/workflow_document.h

DISTFILES += ../src/third_party/qcustomplot/COPYING

SOURCES += ../src/ui/services/export_controller.cpp \
           ../src/ui/panels/plot_style_editor.cpp
HEADERS += ../src/ui/services/export_controller.h \
           ../src/ui/results/plot_style.h \
           ../src/ui/panels/plot_style_editor.h

SOURCES += ../src/ui/panels/file_options_editor.cpp
HEADERS += ../src/ui/panels/file_options_editor.h

SOURCES += ../src/ui/services/record_directory_service.cpp ../src/ui/panels/record_filter_panel.cpp
HEADERS += ../src/ui/services/record_directory_service.h ../src/ui/panels/record_filter_panel.h

SOURCES += ../src/ui/results/plot_workspace.cpp
HEADERS += ../src/ui/results/plot_workspace.h

SOURCES += ../src/ui/panels/axis_details_editor.cpp
HEADERS += ../src/ui/panels/axis_details_editor.h

RESOURCES += ../resources/aster.qrc
SOURCES += ../src/ui/results/plot_probe.cpp
HEADERS += ../src/ui/results/plot_probe.h
SOURCES += ../src/ui/settings/theme_manager.cpp \
           ../src/ui/settings/settings_dialog.cpp \
           ../src/ui/panels/canvas_details_editor.cpp
HEADERS += ../src/ui/settings/theme_manager.h \
           ../src/ui/settings/settings_dialog.h \
           ../src/ui/panels/canvas_details_editor.h \
           ../src/ui/panels/color_button.h

SOURCES += ../src/ui/services/plot_data_loader.cpp \
           ../src/ui/exports/figure_export.cpp \
           ../src/ui/exports/figure_export_controller.cpp \
           ../src/ui/exports/figure_export_dialog.cpp
HEADERS += ../src/ui/services/plot_data_loader.h \
           ../src/ui/exports/figure_export.h \
           ../src/ui/exports/figure_export_controller.h \
           ../src/ui/exports/figure_export_dialog.h
SOURCES += ../src/ui/settings/language_catalog.cpp
HEADERS += ../src/ui/settings/language_catalog.h

SOURCES += ../src/ui/panels/result_export_panel.cpp
HEADERS += ../src/ui/panels/result_export_panel.h

SOURCES += ../src/ui/common/error_presenter.cpp
HEADERS += ../src/ui/common/error_presenter.h

SOURCES += ../src/ui/services/file_preview_loader.cpp ../src/ui/panels/file_preview_panel.cpp
HEADERS += ../src/ui/services/file_preview_loader.h ../src/ui/panels/file_preview_panel.h

SOURCES += ../src/ui/common/parameter_icons.cpp
HEADERS += ../src/ui/common/parameter_icons.h

SOURCES += ../src/ui/panels/heatmap_style_editor.cpp \
           ../src/ui/results/grid_contours.cpp \
           ../src/ui/results/contour_plottable.cpp \
           ../src/ui/results/record_item_delegate.cpp
HEADERS += ../src/ui/panels/style_section.h \
           ../src/ui/panels/heatmap_style_editor.h \
           ../src/ui/results/grid_contours.h \
           ../src/ui/results/contour_plottable.h \
           ../src/ui/results/record_item_delegate.h

SOURCES += ../src/ui/services/workflow_preview_controller.cpp \
           ../src/ui/panels/workflow_preview_panel.cpp \
           ../src/ui/results/preview_data.cpp
HEADERS += ../src/ui/services/workflow_preview_controller.h \
           ../src/ui/panels/workflow_preview_panel.h \
           ../src/ui/results/preview_data.h

SOURCES += ../src/ui/common/task_drag_preview.cpp
HEADERS += ../src/ui/common/task_drag_preview.h

SOURCES += ../src/ui/results/record_directory_panel.cpp
HEADERS += ../src/ui/results/record_directory_panel.h

SOURCES += ../src/ui/workflow/workflow_layout.cpp
HEADERS += ../src/ui/workflow/workflow_layout.h

SOURCES += ../src/ui/common/source_provenance.cpp
HEADERS += ../src/ui/common/source_provenance.h

SOURCES += ../src/update/release_version.cpp ../src/update/release_feed.cpp ../src/update/update_checker.cpp \
           ../src/ui/settings/update_dialog.cpp
HEADERS += ../src/update/release_version.h ../src/update/release_feed.h ../src/update/update_checker.h \
           ../src/ui/settings/update_dialog.h

HEADERS += ../src/ui/common/numeric_editor.h
