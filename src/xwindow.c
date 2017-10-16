#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <xcb/xcb.h>

#define VERSION "0.1"

#define DEFAULT_MAX_TITLE_LENGTH 60
#define DEFAULT_MAX_TITLE_LENGTH_S "60"

#define DEFAULT_SUFFIX "…"
#define DEFAULT_SUFFIX_LENGTH 3

enum xwindow_mode {
    SINGLE,
    FOLLOW
};

typedef struct xwindow_options {
    enum xwindow_mode mode;
    size_t max_title_length;
    char* suffix;
    size_t suffix_length;
    bool ignore_empty_title;
} xwindow_options_t;

typedef struct xwindow_xatoms {
    xcb_atom_t NET_ACTIVE_WINDOW;
    xcb_atom_t NET_WM_NAME;
    xcb_atom_t UTF8_STRING;
} xwindow_xatoms_t;

volatile sig_atomic_t stop = 0;

void help(const char* arg0) {
    fprintf(stderr,
        "%s:\n"
        "Usage: xwindow [OPTION]\n"
        "  -f, --follow                follow active window changes; every time\n"
        "                                the active window changes, its title\n"
        "                                will be printed to stdout\n"
        "  -l, --max-title-length=NUM  specify the maximum length of a window title;\n"
        "                                when the window title exceeds NUM, the suffix\n"
        "                                will be appended to it; this does not include\n"
        "                                the length of the suffix, so the maximum length\n"
        "                                of the output will be NUM + length(SUFFIX)\n"
        "                                the default max title length is " DEFAULT_MAX_TITLE_LENGTH_S "\n"
        "  -s, --suffix=SUFFIX         the suffix to append to the title in case it\n"
        "                                exceeds the maximum title length;\n"
        "                                the default suffix is '" DEFAULT_SUFFIX "'\n"
        "  -i, --ignore-empty-title    never print empty window titles\n"
        "      --help                  display this help and exit\n"
        "      --version               display version information and exit\n"
        "\n"
        "Examples:\n"
        "  %s -f -l 35 --suffix=___  Follow active window changes, set the\n"
        "                                   maximum title length to 35 and the suffix\n"
        "                                   to ___; the maximum length of the title output\n"
        "                                   will be 38 (35 + 3 from the suffix)\n"
        "\n"
        "When no options are provided, xwindow will just print out the title of the\n"
        "current active window and exit (will output and empty line if there is no\n"
        "active window, unless --ignore-empty-title is provided).\n", arg0, arg0
    );

    exit(EXIT_FAILURE);
}

void try_help(const char* arg0) {
    printf("Try '%s --help' for more information.\n", arg0);
    exit(EXIT_FAILURE);
}

void version(void) {
    printf("xwindow " VERSION "\n");
    exit(EXIT_SUCCESS);
}

xcb_atom_t xwindow_get_atom(xcb_connection_t* conn, char* name) {
    xcb_atom_t atom = XCB_ATOM_NONE;
    xcb_intern_atom_reply_t* reply;

    reply = xcb_intern_atom_reply(
        conn,
        xcb_intern_atom(conn, 1, (uint16_t) strlen(name), name),
        NULL
    );

    if (reply) {
        atom = reply->atom;
    }

    free(reply);

    if (atom == XCB_ATOM_NONE) {
        fprintf(stderr, "An error occurred while getting the %s atom!\n", name);
        exit(EXIT_FAILURE);
    }

    return atom;
}

xcb_window_t xwindow_get_active_window(xcb_connection_t* conn, xcb_window_t root_window, xwindow_xatoms_t* xatoms) {
    xcb_window_t active_window = XCB_WINDOW_NONE;
    xcb_get_property_reply_t* reply;

    reply = xcb_get_property_reply(
        conn,
        xcb_get_property(
            conn,
            0,
            root_window,
            xatoms->NET_ACTIVE_WINDOW,
            XCB_ATOM_WINDOW,
            0,
            sizeof(xcb_window_t)
        ),
        NULL
    );

    if (reply) {
        active_window = *(xcb_window_t*) xcb_get_property_value(reply);
    }

    free(reply);
    return active_window;
}

void xwindow_get_wm_name(xcb_connection_t* conn, xcb_window_t window, xwindow_xatoms_t* xatoms, char* wm_name, xwindow_options_t* options) {
    xcb_get_property_reply_t* wm_name_reply;

    wm_name_reply = xcb_get_property_reply(
        conn,
        xcb_get_property(
            conn,
            0,
            window,
            xatoms->NET_WM_NAME,
            xatoms->UTF8_STRING,
            0,
            (uint32_t) (options->max_title_length)
        ),
        NULL
    );

    if (wm_name_reply) {
        size_t wm_name_reply_length = (size_t) xcb_get_property_value_length(wm_name_reply);
        char* wm_name_reply_value = xcb_get_property_value(wm_name_reply);

        if (wm_name_reply_length > options->max_title_length) {
            memcpy(wm_name, wm_name_reply_value, options->max_title_length);
            memcpy(wm_name + options->max_title_length, options->suffix, options->suffix_length);
            wm_name[options->max_title_length + options->suffix_length] = '\0';
        } else {
            memcpy(wm_name, wm_name_reply_value, wm_name_reply_length);
            wm_name[wm_name_reply_length] = '\0';
        }
    } else {
        *wm_name = 0;
    }

    free(wm_name_reply);
}

void print_wm_name(char* wm_name, bool ignore_empty_title) {
    if (strlen(wm_name) > 0 || !ignore_empty_title) {
        printf("%s\n", wm_name);
    }
}

void xwindow_hook(xcb_connection_t* conn, xcb_window_t window) {
    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t value = XCB_EVENT_MASK_PROPERTY_CHANGE;

    xcb_change_window_attributes(conn, window, mask, &value);
    xcb_flush(conn);
}

void xwindow_unhook(xcb_connection_t* conn, xcb_window_t window) {
    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t value = XCB_EVENT_MASK_NO_EVENT;

    xcb_change_window_attributes(conn, window, mask, &value);
    xcb_flush(conn);
}

void sig_handler(int signum) {
    (void) signum;
    stop = 1;
}

void setup_signals() {
    struct sigaction action;
    action.sa_handler = sig_handler;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
}

void xwindow_run(xcb_connection_t* conn, xwindow_options_t* options) {
    char* wm_name;
    xcb_window_t root_window;
    xcb_window_t active_window;

    // Get the NetWM atoms
    xwindow_xatoms_t xatoms = {
        .NET_ACTIVE_WINDOW = xwindow_get_atom(conn, "_NET_ACTIVE_WINDOW"),
        .NET_WM_NAME = xwindow_get_atom(conn, "_NET_WM_NAME"),
        .UTF8_STRING = xwindow_get_atom(conn, "UTF8_STRING"),
    };

    wm_name = malloc(options->max_title_length + options->suffix_length + 1);

    root_window = xcb_setup_roots_iterator(
        xcb_get_setup(conn)
    ).data->root;

    if (root_window == XCB_WINDOW_NONE) {
        fprintf(stderr, "Failed to find the root window");
        exit(EXIT_FAILURE);
    }

    active_window = xwindow_get_active_window(conn, root_window, &xatoms);

    xwindow_get_wm_name(conn, active_window, &xatoms, wm_name, options);
    print_wm_name(wm_name, options->ignore_empty_title);

    if (options->mode == SINGLE) {
        free(wm_name);
        return;
    }

    setup_signals();

    xwindow_hook(conn, root_window);
    xwindow_hook(conn, active_window);

    while (!stop) {
        xcb_generic_event_t* event = xcb_poll_for_event(conn);
        if (!event) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 250000000L;
            nanosleep(&ts, NULL);
            continue;
        }

        if ((event->response_type & ~0x80) == XCB_PROPERTY_NOTIFY) {
            xcb_property_notify_event_t* notify = (xcb_property_notify_event_t* ) event;
            if (notify->atom != xatoms.NET_ACTIVE_WINDOW && notify->atom != xatoms.NET_WM_NAME) {
                free(event);
                continue;
            }

            if (notify->atom == xatoms.NET_ACTIVE_WINDOW) {
                xwindow_unhook(conn, active_window);

                active_window = xwindow_get_active_window(conn, root_window, &xatoms);
                if (active_window != root_window) {
                    xwindow_hook(conn, active_window);
                }
            } else if (notify->atom == xatoms.NET_WM_NAME) {
                if (notify->window != active_window) {
                    free(event);
                    continue;
                }
            }

            xwindow_get_wm_name(conn, active_window, &xatoms, wm_name, options);
            print_wm_name(wm_name, options->ignore_empty_title);
        }

        free(event);
    }

    xwindow_unhook(conn, active_window);
    xwindow_unhook(conn, root_window);
    free(wm_name);
}

int main(int argc, char* argv[]) {
    xcb_connection_t* conn;
    xwindow_options_t options = {
        .mode = SINGLE,
        .max_title_length = DEFAULT_MAX_TITLE_LENGTH,
        .suffix = DEFAULT_SUFFIX,
        .suffix_length = DEFAULT_SUFFIX_LENGTH,
        .ignore_empty_title = false,
    };

    static const struct option long_options[] = {
        {"follow", optional_argument, NULL, 'f'},
        {"max-title-length", optional_argument, NULL, 'l'},
        {"version", optional_argument, NULL, 'v'},
        {"help", optional_argument, NULL, 'h'},
        {"suffix", optional_argument, NULL, 's'},
        {"ignore-empty-title", optional_argument, NULL, 'i'},
        {NULL, 0, NULL, 0}
    };

    int option_index;
    int c;
    while ((c = getopt_long(argc, argv, "fl:vhs:i?", long_options, &option_index)) != -1) {
        switch (c) {
            case 'v': {
                version();
                break;
            }
            case 'h': {
                help(argv[0]);
                break;
            }
            case 'f': {
                options.mode = FOLLOW;
                break;
            }
            case 'l': {
                char* ptr;
                size_t max_title_length = strtoul(optarg, &ptr, 10);
                if (max_title_length == ULONG_MAX && errno == ERANGE) {
                    printf("%s: max-title-length invalid: '%s'\n", argv[0], optarg);
                    try_help(argv[0]);
                }
                options.max_title_length = max_title_length;
                break;
            }
            case 's': {
                options.suffix = optarg;
                options.suffix_length = strlen(options.suffix);
                break;
            }
            case 'i': {
                options.ignore_empty_title = true;
                break;
            }
            case '?':
            default: {
                try_help(argv[0]);
                break;
            }
        }
    }

    if (optind < argc) {
        printf("%s: invalid argument: '%s'\n", argv[0], argv[optind]);
        try_help(argv[0]);
    }

    setlocale(LC_ALL, "");
    setbuf(stdout, NULL);

    conn = xcb_connect(NULL, NULL);
    if (conn == NULL || xcb_connection_has_error(conn)) {
        fprintf(stderr, "Failed to connect to display\n");
        exit(EXIT_FAILURE);
    }

    xwindow_run(conn, &options);
    xcb_disconnect(conn);

    return EXIT_SUCCESS;
}