#include "include.h"

void async_free(struct async **as)
{
	if(!*as) return;
	free(*as);
	*as=NULL;
}

static void async_settimers(struct async *as, int sec, int usec)
{
	as->setsec=sec;
	as->setusec=usec;
}

static int async_rw(struct async *as)
{
        int mfd=-1;
        fd_set fsr;
        fd_set fsw;
        fd_set fse;
	int doread=0;
	int dowrite=0;
        struct timeval tval;

	if(as->doing_estimate) return 0;

	if(as->asfd->rbuf) doread++;

	if(as->asfd->writebuflen && !as->asfd->write_blocked_on_read)
		dowrite++; // The write buffer is not yet empty.

	if(doread)
	{
		if(as->asfd->parse_readbuf(as->asfd)) return -1;
		if(as->asfd->rbuf->buf && !as->asfd->writebuflen) return 0;

		if(as->asfd->read_blocked_on_write) doread=0;
	}

        if(doread || dowrite)
        {
                mfd=-1;

                if(doread) FD_ZERO(&fsr);
                if(dowrite) FD_ZERO(&fsw);
                FD_ZERO(&fse);

                add_fd_to_sets(as->asfd->fd,
			doread?&fsr:NULL, dowrite?&fsw:NULL, &fse, &mfd);

                tval.tv_sec=as->setsec;
                tval.tv_usec=as->setusec;

                if(select(mfd+1,
			doread?&fsr:NULL, dowrite?&fsw:NULL, &fse, &tval)<0)
                {
                        if(errno!=EAGAIN && errno!=EINTR)
                        {
                                logp("select error in %s: %s\n", __func__,
					strerror(errno));
                                return -1;
                        }
                }

		if(!FD_ISSET(as->asfd->fd, &fse)
		  && (!doread || !FD_ISSET(as->asfd->fd, &fsr))
		  && (!dowrite || !FD_ISSET(as->asfd->fd, &fsw)))
		{
			// Be careful to avoid 'read quick' mode.
			if((as->setsec || as->setusec)
			  && as->asfd->max_network_timeout>0
			  && as->asfd->network_timeout--<=0)
			{
				logp("No activity on network for %d seconds.\n",
					as->asfd->max_network_timeout);
				return -1;
			}
			return 0;
		}
		as->asfd->network_timeout=as->asfd->max_network_timeout;

                if(FD_ISSET(as->asfd->fd, &fse))
                {
                        logp("error on socket\n");
                        return -1;
                }

                if(doread && FD_ISSET(as->asfd->fd, &fsr)) // able to read
                {
			if(as->asfd->ssl)
			{
				as->asfd->read_blocked_on_write=0;
				if(as->asfd->do_read_ssl(as->asfd)) return -1;
			}
			else
			{
				if(as->asfd->do_read(as->asfd)) return -1;
			}
			return as->asfd->parse_readbuf(as->asfd);
                }

                if(dowrite && FD_ISSET(as->asfd->fd, &fsw)) // able to write
		{
			if(as->asfd->ssl)
			{
				as->asfd->write_blocked_on_read=0;
				return as->asfd->do_write_ssl(as->asfd);
			}
			else
				return as->asfd->do_write(as->asfd);
		}
        }

        return 0;
}

static int async_read_quick(struct async *as)
{
	int r;
	int savesec=as->setsec;
	int saveusec=as->setusec;
	as->setsec=0;
	as->setusec=0;
	r=as->rw(as);
	as->setsec=savesec;
	as->setusec=saveusec;
	return r;
}

static void async_add_asfd(struct async *as, struct asfd *asfd)
{
	struct asfd *x;
	if(!as->asfd)
	{
		as->asfd=asfd;
		return ;
	}
	// Add to the end;
	for(x=as->asfd; x->next; x=x->next) { }
	x->next=asfd;
}

static int async_init(struct async *as, int estimate)
{
	as->setsec=1;
	as->setusec=0;
	as->doing_estimate=estimate;

	as->rw=async_rw;
	as->read_quick=async_read_quick;

	as->settimers=async_settimers;
	as->add_asfd=async_add_asfd;

	return 0;
}

struct async *async_alloc(void)
{
	struct async *as;
	if(!(as=(struct async *)calloc_w(1, sizeof(struct async), __func__)))
		return NULL;
	as->init=async_init;
	return as;
}