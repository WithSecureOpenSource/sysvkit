#include "cn_proc.h"

#include "common.h"
#include "noise.h"

#include <errno.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

static int nld = -1;

static struct sockaddr_nl sanl = {
    .nl_family = AF_NETLINK,
    .nl_groups = CN_IDX_PROC,
};

static bool listening;

// Connects to the process event connector.
bool cn_proc_connect(void)
{
    if (nld >= 0) {
        return true;
    }
    nld = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (nld < 0) {
        error("failed to open netlink socket: %m");
        return false;
    }
    sanl.nl_pid = getpid();
    if (bind(nld, (struct sockaddr *)&sanl, sizeof(sanl)) != 0) {
        error("failed to bind netlink socket: %m");
        close(nld);
        nld = -1;
        return false;
    }
    return true;
}

// Disconnects from the process event connector.
void cn_proc_disconnect(void)
{
    if (nld < 0) {
        return;
    }
    if (listening) {
        (void)cn_proc_listen(false, 1000);
    }
    close(nld);
    nld = -1;
}

// Sends a process event connector message.
ssize_t cn_proc_send(const void *data, size_t len)
{
    struct nlmsghdr nlmsg = {};
    struct cn_msg cnmsg = {};
    struct msghdr msg = {};
    struct iovec iov[3];
    ssize_t res;

    // netlink header
    nlmsg.nlmsg_len = NLMSG_LENGTH(sizeof(cnmsg) + len);
    nlmsg.nlmsg_seq = sanl.nl_pid;
    nlmsg.nlmsg_type = NLMSG_DONE;
    iov[0].iov_base = &nlmsg;
    iov[0].iov_len = sizeof(nlmsg);
    // connector header
    cnmsg.id.idx = CN_IDX_PROC;
    cnmsg.id.val = CN_VAL_PROC;
    cnmsg.len = len;
    iov[1].iov_base = &cnmsg;
    iov[1].iov_len = sizeof(cnmsg);
    // message
    iov[2].iov_base = DQ(data);
    iov[2].iov_len = len;
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;
    // ship it
    res = sendmsg(nld, &msg, 0);
    if (res < 0) {
        return -1;
    }
    if ((size_t)res != iov[0].iov_len + iov[1].iov_len + iov[2].iov_len) {
        errno = ECOMM;
        return -1;
    }
    return res;
}

// Receives a process event connector message.  The timeout is in milliseconds
// with the same semantics as for poll(2).
ssize_t cn_proc_receive(void *buf, size_t size, int timeout)
{
    struct nlmsghdr nlmsg = {};
    struct cn_msg cnmsg = {};
    struct msghdr msg = {};
    struct iovec iov[3];
    struct pollfd pfd;
    ssize_t res;

    // Wait for a message to arrive
    pfd.fd = nld;
    pfd.events = POLLIN;
    res = poll(&pfd, 1, timeout);
    if (res < 0) {
        return -1;
    }
    if (res == 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    if (pfd.revents & POLLERR) {
        errno = EPIPE;
        return -1;
    }
    if (!(pfd.revents & POLLIN)) {
        errno = EIO; // find a better errno
        return -1;
    }

    // Receive the message
    iov[0].iov_base = &nlmsg;
    iov[0].iov_len = sizeof(nlmsg);
    iov[1].iov_base = &cnmsg;
    iov[1].iov_len = sizeof(cnmsg);
    iov[2].iov_base = buf;
    iov[2].iov_len = size;
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;
    res = recvmsg(nld, &msg, MSG_TRUNC);
    if (res < 0) {
        if (errno != ETIMEDOUT) {
            error("process connector rx error: %m");
        }
        return -1;
    }

    // Validate it
    if ((size_t)res < sizeof(nlmsg) || (size_t)res != nlmsg.nlmsg_len) {
        warning("incomplete netlink header");
        errno = EPROTO;
        return -1;
    }
    res -= sizeof(nlmsg);
    if ((size_t)res < sizeof(cnmsg)) {
        warning("incomplete connector header");
        errno = EPROTO;
        return -1;
    }
    if (cnmsg.id.idx != CN_IDX_PROC || cnmsg.id.val != CN_VAL_PROC) {
        warning("invalid connector id %u:%u", cnmsg.id.idx, cnmsg.id.val);
        errno = EPROTO;
        return -1;
    }
    res -= sizeof(cnmsg);
    if ((size_t)res != cnmsg.len) {
        warning("invalid process event message length");
        errno = EPROTO;
        return -1;
    }
    return res;
}

// Receives a process event.  The timeout is in milliseconds with the same
// semantics as for poll(2).
bool cn_proc_receive_event(struct proc_event *ev, int timeout)
{
    ssize_t rlen;

    memset(ev, 0, sizeof(*ev));
    rlen = cn_proc_receive(ev, sizeof(*ev), timeout);
    if (rlen < 0) {
        return false;
    }
    if ((size_t)rlen < PROC_EVENT_MIN_SIZE) {
        fatal("struct proc_event size mismatch");
    }
    return true;
}

// Enables or disables process events.  The timeout is in milliseconds with the
// same semantics as for poll(2), but may be applied multiple times in
// succession.
bool cn_proc_listen(bool endis, int timeout)
{
    struct proc_event ev = {};
    struct proc_ctl ctl = {};

    if (endis == listening) {
        return true;
    }
    // send the listen / ignore message
    verbose("%sabling process event stream", endis ? "en" : "dis");
    ctl.op = endis ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;
    if (!cn_proc_send(&ctl, sizeof(ctl))) {
        error("failed to send");
        return false;
    }
    // wait for ack, check error code
    while (cn_proc_receive_event(&ev, timeout)) {
        if (ev.what == PROC_EVENT_NONE) {
            if (ev.ack.err != 0) {
                debug("cn_proc: error %u", ev.ack.err);
                errno = ev.ack.err;
                return false;
            }
            debug("cn_proc: success");
            listening = endis;
            return true;
        }
    }
    // I/O error or timed out waiting for ack.  This is expected in the disable
    // case, because the kernel checks the listener count before sending the
    // ack, _after_ decrementing it.  If there are no other listeners, the
    // listener count will be zero, and the ack will never be sent.  Conversely,
    // if there are other listeners, they will all receive our ack.
    if (errno == ETIMEDOUT && endis) {
        debug("timed out waiting for event connector %sable ack",
              endis ? "en" : "dis");
    }

    // If disabling, we have to assume that we succeeded, because there are _no_
    // checks in the kernel, and if we try again we risk decrementing the
    // reference count to a negative number, meaning that the next time we try
    // to enable monitoring we will only raise the count to zero (or _towards_
    // zero).
    //
    // Conversely, if enabling, we have to assume that we failed, otherwise the
    // cleanup code may incorrectly decrement the reference count.
    //
    // In summary: for every successful enable there must be _at most_ one
    // successful disable.  A lost disable will, at worst, cause an
    // infinitesimal drop in performance, but a lost enable (or duplicate
    // disable) will break the application.
    //
    // XXX we should consider installing a signal handler that disables
    // listening if a killing signal is received, possibly also an atexit()
    // handler.  But we have to be careful around forks, so perhaps the
    // listening flag should not be a bool but the pid of the process that
    // started listening.  Don't you just love Linux?
    if (!endis) {
        listening = false;
    }
    return false;
}

// Returns a file descriptor that can be used to poll for events.  If not
// connected, returns -1 and sets errno to EBADF.
int cn_proc_fd(void)
{
    if (nld < 0) {
        errno = EBADF;
        return -1;
    }
    return nld;
}
