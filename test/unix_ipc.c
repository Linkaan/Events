#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event-config.h>
#include <event2/thread.h>

static void
accept_error_cb (struct evconnlistener *listener, void *arg)
{
    int err;
    struct event_base *base;

    base = evconnlistener_get_base (listener);
    err = EVUTIL_SOCKET_ERROR ();
    printf ("fgevents: %s: %d: Error when listening on events (%d): %s\n",
              __FILE__, __LINE__, err, evutil_socket_error_to_string (err));
    event_base_loopexit (base, NULL);
}

static int
setup_unix (struct event_base *base, struct evconnlistener **listener, char *unix_path)
{
    struct sockaddr_un sun;
    struct evconnlistener *_listener;

    if (unix_path == NULL)
        return 0;    

    memset (&sun, 0, sizeof (sun));
    sun.sun_family = AF_LOCAL;
    strncpy (sun.sun_path, unix_path, sizeof (sun.sun_path) - 1);

    unlink (unix_path);

    _listener = evconnlistener_new_bind (base, NULL, NULL,
                                         LEV_OPT_REUSEABLE |
                                         LEV_OPT_CLOSE_ON_FREE, -1,
                                         (struct sockaddr *) &sun,
                                         sizeof (sun));
    if (_listener == NULL)
      {
        //report_error_en (itdata, EAGAIN, "Could not create unix listener");
        perror ("Could not create unix listener");
        return -1;
      }

    evconnlistener_set_error_cb (_listener, &accept_error_cb);

    *listener = _listener;

    return 0;
}

int
main(void)
{
	struct evconnlistener *listener;
	struct event_base *base = event_base_new ();

	if (base == NULL)
      {
      	printf("Could not create libevent base\n");
      	return 1;
      }

    setup_unix (base, &listener, "/tmp/unix_ipc.sock");

    evconnlistener_free (listener);

    event_base_free (base);

    printf ("Success!\n");

    return 0;
}