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

#include <map>
#include <memory>
#include <wayfire/plugin.hpp>

#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/input-grab.hpp>

/** To read VSK Settings */
#include <QSettings>
#include <QStringList>

#include "VSKShell.hpp"

static QString cleanPath( QString path );

static void configureView( wayfire_view, wf::output_t * );

void VSK::Shell::PluginImpl::init() {
    /** A new view was just added: Set the various properties if it's the correct view */
    wf::get_core().connect( &onViewAddedSignal );

    /** Configure the VSK clients as Shell Components */
    output->connect( &onViewMappedSignal );

    /** Unset the Shell component status and clear the pointers */
    output->connect( &onViewVanishedSignal );

    /** Do not focus notification views */
    output->connect( &onPreViewFocused );

    QString panelPath = cleanPath( panel_config.value().length() ? QString( panel_config.value().c_str() ) : defPanelPath );
    panelCfg = new QSettings( panelPath, QSettings::IniFormat );
    QString runnerPath = cleanPath( runner_config.value().length() ? QString( runner_config.value().c_str() ) : defRunnerPath );
    runnerCfg = new QSettings( runnerPath, QSettings::IniFormat );
    QString notifyPath = cleanPath( notify_config.value().length() ? QString( notify_config.value().c_str() ) : defNotifyPath );
    notifyCfg = new QSettings( notifyPath, QSettings::IniFormat );

    panel_config.set_callback(
        [ = ] () {
            QString panelPath = (panel_config.value().length() ? QString( panel_config.value().c_str() ) : defPanelPath);
            panelCfg          = new QSettings( panelPath, QSettings::IniFormat );

            /** Si el panel se ejecuta en esta salida, reposicione*/
            if ( panels[ output ].viewTop ) {
                setViewAsPanel( panels[ output ].viewTop, output );
            }
        }
    );

    runner_config.set_callback(
        [ = ] () {
            QString runnerPath = (runner_config.value().length() ? QString( runner_config.value().c_str() ) : defRunnerPath);
            runnerCfg          = new QSettings( runnerPath, QSettings::IniFormat );

            /** If runner is running on this output, reposition it */
            if ( mRunnerView ) {
                showRunner( mRunnerView, output );
            }
        }
    );

    notify_config.set_callback(
        [ = ] () {
            QString notifyPath = (notify_config.value().length() ? QString( notify_config.value().c_str() ) : defNotifyPath);
            notifyCfg          = new QSettings( notifyPath, QSettings::IniFormat );

            /** If notify is running on this output, reposition it */
            if ( mNotifyView ) {
            }
        }
    );

    std::string command = session_command.value();

    if ( command.empty() ) {
        command = "vasak-session";
    }

    /** If the user wants us to start the session */
    if ( start_session.value() ) {
        wf::get_core().run( std::string( command ) );
    }
}


void VSK::Shell::PluginImpl::fini() {
    if ( backgrounds[ output ].view ) {
        backgrounds[ output ].view->close();
    }

    if ( panels[ output ].viewTop ) {
        panels[ output ].viewTop->close();
    }

    if ( panels[ output ].viewLeft ) {
        panels[ output ].viewLeft->close();
    }

    if ( panels[ output ].anchorTop ) {
        output->workspace->remove_reserved_area( panels[ output ].anchorTop.get() );
        panels[ output ].anchorTop = nullptr;
    }

    if ( panels[ output ].anchorLeft ) {
        output->workspace->remove_reserved_area( panels[ output ].anchorLeft.get() );
        panels[ output ].anchorLeft = nullptr;
    }

    delete panelCfg;
    delete runnerCfg;
    delete notifyCfg;
}


void VSK::Shell::PluginImpl::setViewAsBackground( wayfire_view view, wf::output_t *output ) {
    backgrounds[ output ].view = view;

    view->set_decoration( nullptr );
    wf::get_core().move_view_to_output( view, output, false );
    view->set_geometry( output->get_relative_geometry() );
    output->workspace->add_view( view, wf::LAYER_BACKGROUND );
    view->sticky = true;
    view->set_role( wf::VIEW_ROLE_DESKTOP_ENVIRONMENT );
}


void VSK::Shell::PluginImpl::setViewAsPanel( wayfire_view view, wf::output_t *output ) {
    view->set_decoration( nullptr );
    wf::get_core().move_view_to_output( view, output, false );

    /** We don't want it above fullscreen views */
    output->workspace->add_view( view, wf::LAYER_TOP );
    view->sticky = true;
    view->set_role( wf::VIEW_ROLE_DESKTOP_ENVIRONMENT );

    configureView( view, output );
    output->workspace->reflow_reserved_areas();
}


void VSK::Shell::PluginImpl::showRunner( wayfire_view view, wf::output_t *output ) {
    mRunnerView = view;

    view->set_decoration( nullptr );
    wf::get_core().move_view_to_output( view, output, false );
    view->set_geometry( view->get_wm_geometry() );

    /** We want it above fullscreen views */
    output->workspace->add_view( view, wf::LAYER_UNMANAGED );
    view->sticky = true;
    view->set_role( wf::VIEW_ROLE_DESKTOP_ENVIRONMENT );

    /** Get the available geometry of the current workspace of the current output */
    auto workarea = view->get_output()->workspace->get_workarea();

    /** Get the geometry of the view */
    auto window = view->get_wm_geometry();

    /** Get the horizontal center */
    window.x = workarea.x + (workarea.width / 2) - (window.width / 2);

    /** Sync before reading the settings */
    runnerCfg->sync();

    /** Slightly below the top of the workspace */
    if ( runnerCfg->value( "dialog/show_on_top" ).toBool() ) {
        window.y = workarea.y + 10;
    }

    /** Center of the workspace */
    else {
        window.y = workarea.y + (workarea.height / 2) - (window.height / 2);
    }

    /** Move the view */
    view->set_geometry( window );
}


void VSK::Shell::PluginImpl::showNotification( wayfire_view view, wf::output_t *output ) {
    /** We need this only when geometry is modified externally. */
    onNotifyViewResized.disconnect();

    mNotifyView = view;

    view->set_decoration( nullptr );
    wf::get_core().move_view_to_output( view, output, false );

    /** We want it above fullscreen views */
    output->workspace->add_view( view, wf::LAYER_TOP );
    view->sticky = true;
    view->set_role( wf::VIEW_ROLE_DESKTOP_ENVIRONMENT );

    /** Sync before reading the settings */
    notifyCfg->sync();

    /** Get the available geometry of the current workspace of the current output */
    auto workarea = view->get_output()->workspace->get_workarea();

    /** Get the geometry of the view */
    auto window = view->get_wm_geometry();

    /** Get the position */
    QString notifyPos = notifyCfg->value( "placement", "top-right" ).toString();

    if ( notifyPos == "top-center" ) {
        window.x = workarea.x + (workarea.width - window.width) / 2;
        window.y = workarea.y + 10;
    }

    else if ( notifyPos == "top-left" ) {
        window.x = workarea.x + 10;
        window.y = workarea.y + 10;
    }

    else if ( notifyPos == "center-left" ) {
        window.x = workarea.x + 10;
        window.y = workarea.y + (workarea.height - window.height) / 2;
    }

    else if ( notifyPos == "bottom-left" ) {
        window.x = workarea.x + 10;
        window.y = workarea.y + workarea.height - window.height - 10;
    }

    else if ( notifyPos == "bottom-center" ) {
        window.x = workarea.x + (workarea.width - window.width) / 2;
        window.y = workarea.y + workarea.height - window.height - 10;
    }

    else if ( notifyPos == "bottom-right" ) {
        window.x = workarea.x + workarea.width - window.width - 10;
        window.y = workarea.y + workarea.height - window.height - 10;
    }

    else if ( notifyPos == "center-right" ) {
        window.x = workarea.x + workarea.width - window.width - 10;
        window.y = workarea.y + (workarea.height - window.height) / 2;
    }

    else {  /** top-right */
        window.x = workarea.x + workarea.width - window.width - 10;
        window.y = workarea.y + 10;
    }

    /** Move the view */
    view->set_geometry( window );

    /** Now that modification is done, let's reconnect the signal */
    view->connect( &onNotifyViewResized );
}


DECLARE_WAYFIRE_PLUGIN( wf::per_output_plugin_t<VSK::Shell::PluginImpl> );

/**
 * CODE TO SET PANEL EXCLUSIVE ZONE
 */
void configureView( wayfire_view view, wf::output_t *output ) {
    /** Get the available geometry of the current workspace of the current output */
    auto workarea = output->workspace->get_workarea();

    /** Get the geometry of the view */
    auto window = view->get_wm_geometry();

    /** Horizontal panel: pin to top */
    if ( window.width > window.height ) {
        /** Ensure panel is of proper width */
        if ( window.width > workarea.width ) {
            window.width = workarea.width;
        }

        window.x = workarea.x + (workarea.width - window.width) / 2;
        window.y = workarea.y;
        panels[ output ].viewTop = view;

        if ( panels[ output ].anchorTop == nullptr ) {
            panels[ output ].anchorTop           = std::make_unique<wf::workspace_manager::anchored_area>();
            panels[ output ].anchorTop->reflowed =
                [ view, output ] (auto, auto) {
                    configureView( view, output );
                };
            output->workspace->add_reserved_area( panels[ output ].anchorTop.get() );
        }

        panels[ output ].anchorTop->edge          = wf::workspace_manager::ANCHORED_EDGE_TOP;
        panels[ output ].anchorTop->reserved_size = window.height;
        panels[ output ].anchorTop->real_size     = window.height;
    }

    /** Horizontal panel: pin to left */
    else {
        /** Ensure panel is of proper height */
        if ( window.height > workarea.height ) {
            window.height = workarea.height;
        }

        window.x = workarea.x;
        window.y = workarea.y + (workarea.height - window.height) / 2;
        panels[ output ].viewLeft = view;

        if ( panels[ output ].anchorLeft == nullptr ) {
            panels[ output ].anchorLeft           = std::make_unique<wf::workspace_manager::anchored_area>();
            panels[ output ].anchorLeft->reflowed =
                [ view, output ] (auto, auto) {
                    configureView( view, output );
                };
            output->workspace->add_reserved_area( panels[ output ].anchorLeft.get() );
        }

        panels[ output ].anchorLeft->edge          = wf::workspace_manager::ANCHORED_EDGE_LEFT;
        panels[ output ].anchorLeft->reserved_size = window.width;
        panels[ output ].anchorLeft->real_size     = window.width;
    }

    /** Set view geometry */
    view->set_geometry( window );
}


/**
 * Replace shell variables and shortcuts with proper paths
 */
QString cleanPath( QString path ) {
    QStringList parts = path.split( "/", Qt::KeepEmptyParts );

    if ( parts.count() ) {
        /** Env variable */
        if ( parts.at( 0 ).startsWith( "$" ) ) {
            parts[ 0 ] = qEnvironmentVariable( parts.at( 0 ).toUtf8().constData(), "" );
        }

        /** Shell shortcut */
        else if ( parts.at( 0 ) == "~" ) {
            parts[ 0 ] = QDir::homePath();
        }

        /** Not an absolute path? Let's assume it's relative to pwd */
        else if ( parts.at( 0 ) != "" ) {
            parts.prepend( QDir::currentPath() );
        }

        return parts.join( "/" );
    }

    return path;
}
