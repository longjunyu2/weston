Weston kiosk-shell
==================

Weston's kiosk-shell is a simple shell targeted at single-app/kiosk use cases.
It makes all top-level application windows fullscreen, and supports defining
which applications to place on particular outputs. This is achieved with the
``app-ids=`` field in the corresponding output section in weston.ini. For
example:

.. code-block:: ini

    [output]
    name=screen0
    app-ids=org.domain.app1,com.domain.app2
    x11-wm-name=xterm,Mozilla Firefox
    x11-wm-class=Navigator

Xwayland windows can be specified either using ``x11-wm-name=``, which matches
the ``WM_NAME`` X11 property and ``x11-wm-class=``, which matches `WM_CLASS`
one. If the Xwayland window has both a ``WM_CLASS`` or a ``WM_NAME`` set, then
it will be checked in both ``x11-wm-name`` and in ``x11-wm-class`` list entry.

To run weston with kiosk-shell set ``shell=kiosk-shell.so`` in weston.ini, or
use the ``--shell=kiosk-shell.so`` command-line option.
