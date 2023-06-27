/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Marcus Britanicus
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 **/

#include <wayland-client.h>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/util/log.hpp>

#include <memory>
#include <wayfire/plugin.hpp>

#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>

#include <QDir>
#include <QString>

namespace VSK {
    namespace Shell {
        class PluginImpl;
    }
}

struct BackgroundView {
    nonstd::observer_ptr<wf::view_interface_t> view;
};

struct PanelView {
    nonstd::observer_ptr<wf::view_interface_t>            viewTop;
    nonstd::observer_ptr<wf::view_interface_t>            viewLeft;
    std::unique_ptr<wf::workspace_manager::anchored_area> anchorTop;
    std::unique_ptr<wf::workspace_manager::anchored_area> anchorLeft;
};

static std::map<wf::output_t *, BackgroundView> backgrounds;
static std::map<wf::output_t *, PanelView>      panels;

class VSK::Shell::PluginImpl : public wf::per_output_plugin_instance_t {
    public:
        void init() override;
        void fini() override;

    private:
        void setViewAsBackground( wayfire_view, wf::output_t *output );
        void setViewAsPanel( wayfire_view, wf::output_t *output );
        void showRunner( wayfire_view, wf::output_t *output );
        void showNotification( wayfire_view, wf::output_t *output );

        wayfire_view mLastFocusView;

        wayfire_view mRunnerView;
        wayfire_view mNotifyView;

        /** VSK settings */
        QSettings *panelCfg  = nullptr;
        QSettings *runnerCfg = nullptr;
        QSettings *notifyCfg = nullptr;

        wf::option_wrapper_t<bool> start_session{ "vsk-shell/start_vsk_session" };
        wf::option_wrapper_t<std::string> session_command{ "vsk-shell/session_command" };

        wf::option_wrapper_t<std::string> panel_config{ "vsk-shell/panel_config_file" };
        wf::option_wrapper_t<std::string> runner_config{ "vsk-shell/runner_config_file" };
        wf::option_wrapper_t<std::string> notify_config{ "vsk-shell/notify_config_file" };

        QString defPanelPath  = QDir::home().filePath( ".config/lxqt/panel.conf" );
        QString defRunnerPath = QDir::home().filePath( ".config/lxqt/lxqt-runner.conf" );
        QString defNotifyPath = QDir::home().filePath( ".config/lxqt/notifications.conf" );

        /** Will be used to set the role of notification view only */
        wf::signal::connection_t<wf::view_added_signal> onViewAddedSignal =
            [ = ] (wf::view_added_signal *ev) {
                /** A view opened on a different output will be handled by that instance of this plugin */
                if ( ev->view->get_output() != output ) {
                    return;
                }

                /**
                 * Set the role of the notification view as DE.
                 * This way, it will not interfere show-desktop of wm-actions.
                 */
                if ( ev->view->get_app_id() == "lxqt-notificationd" ) {
                    if ( ev->view->get_title() == "lxqt-notificationd" ) {
                        ev->view->role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;
                    }
                }
            };

        /** Will be used to position the views */
        wf::signal::connection_t<wf::view_mapped_signal> onViewMappedSignal =
            [ = ] (wf::view_mapped_signal *ev) {
                /** A view opened on a different output will be handled by that instance of this plugin */
                if ( ev->view->get_output() != output ) {
                    return;
                }

                wayfire_view view  = ev->view;
                std::string  appId = view->get_app_id();

                /** Vasak Desktop: Probably started in desktop mode */
                if ( appId == "vasak-desktop" ) {
                    std::string title = view->get_title();

                    if ( (title == "Vasak Desktop") ) {
                        for ( auto& op : wf::get_core().output_layout->get_outputs() ) {
                            if ( backgrounds[ op ].view ) {
                                continue;
                            }

                            setViewAsBackground( view, op );
                            ev->is_positioned = true;

                            return;
                        }
                    }
                }

                /** Navale: Probably started in panel mode */
                else if ( appId == "navale" ) {
                    std::string title = view->get_title();

                    if ( title == "Navale" ) {
                        for ( auto& op : wf::get_core().output_layout->get_outputs() ) {
                            if ( panels[ op ].viewTop and panels[ op ].viewLeft ) {
                                continue;
                            }

                            setViewAsPanel( view, op );
                            ev->is_positioned = true;

                            return;
                        }
                    }
                }

                /** Hydriam: Probably started in menu mode */
                else if ( appId == "hydriam" ) {
                    std::string title = view->get_title();

                    if ( title == "Hydriam" ) {
                        wf::output_t *output = wf::get_core().get_active_output();

                        if ( output != nullptr ) {
                            showRunner( view, output );
                            ev->is_positioned = true;
                        }
                    }
                }

                /** PCManFM Qt: Probably started in desktop mode */
                else if ( appId == "lxqt-notificationd" ) {
                    std::string title = view->get_title();

                    if ( title == "lxqt-notificationd" ) {
                        wf::output_t *output = wf::get_core().get_active_output();

                        if ( output != nullptr ) {
                            showNotification( view, output );
                            ev->is_positioned = true;
                        }
                    }
                }
            };

        /** Used */
        wf::signal::connection_t<wf::view_disappeared_signal> onViewVanishedSignal =
            [ = ] ( wf::view_disappeared_signal *ev ) {
                if ( ev->view and backgrounds[ output ].view == ev->view ) {
                    backgrounds[ output ].view = nullptr;
                }

                if ( ev->view and panels[ output ].viewTop == ev->view ) {
                    panels[ output ].viewTop = nullptr;

                    if ( panels[ output ].anchorTop ) {
                        output->workspace->remove_reserved_area( panels[ output ].anchorTop.get() );
                        panels[ output ].anchorTop = nullptr;
                        output->workspace->reflow_reserved_areas();
                    }
                }

                if ( ev->view and panels[ output ].viewLeft == ev->view ) {
                    panels[ output ].viewLeft = nullptr;

                    if ( panels[ output ].anchorLeft ) {
                        output->workspace->remove_reserved_area( panels[ output ].anchorLeft.get() );
                        panels[ output ].anchorLeft = nullptr;
                        output->workspace->reflow_reserved_areas();
                    }
                }

                if ( ev->view == mLastFocusView ) {
                    mLastFocusView = nullptr;
                }

                if ( ev->view == mRunnerView ) {
                    mRunnerView = nullptr;
                }

                if ( ev->view == mNotifyView ) {
                    mNotifyView = nullptr;
                }
            };

        /** Used to disable focus of notification views */
        wf::signal::connection_t<wf::pre_focus_view_signal> onPreViewFocused =
            [ = ] (wf::pre_focus_view_signal *ev) {
                if ( ev->view ) {
                    std::string appId = ev->view->get_app_id();
                    std::string title = ev->view->get_title();

                    if ( (appId == "lxqt-notificationd") and (title == "lxqt-notificationd") ) {
                        ev->can_focus = false;

                        if ( mLastFocusView ) {
                            /** May be show-dessktop is active. */

                            /** Source 1: wm-actions */
                            if ( mLastFocusView->has_data( "wm-actions-showdesktop" ) ) {
                                return;
                            }

                            /** Source 2: dbusqt plugin */
                            if ( mLastFocusView->has_data( "dbusqt-showdesktop" ) ) {
                                return;
                            }

                            /** Source 3: wayfire-workspaces-unstable-v1 */
                            if ( mLastFocusView->has_data( "wf-workspaces-showdesktop" ) ) {
                                return;
                            }

                            /** Show-desktop is not active. So refocus the last view */
                            output->workspace->bring_to_front( mLastFocusView );
                        }
                    }

                    else {
                        mLastFocusView = ev->view;
                    }
                }
            };

        /** To reposition the notification views */
        wf::signal::connection_t<wf::view_geometry_changed_signal> onNotifyViewResized =
            [ = ] (wf::view_geometry_changed_signal *ev) {
                if ( ev->view and (ev->view == mNotifyView) ) {
                    showNotification( ev->view, output );
                }
            };
};
