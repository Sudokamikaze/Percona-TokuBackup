/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:

#include "file_description.h"
#include "real_syscalls.h"
#include "backup_debug.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const int DEST_FD_INIT = -1;

///////////////////////////////////////////////////////////////////////////////
//
// file_description() -
//
// Description: 
//
//     ...
//
file_description::file_description(void)
: m_refcount(1), 
m_offset(0), 
m_fd_in_dest_space(DEST_FD_INIT), 
m_backup_name(NULL),
m_full_source_name(NULL), 
m_in_source_dir(false)
{
    int r = pthread_mutex_init(&m_mutex, NULL);
    assert(r==0);
}

file_description::~file_description(void)
{
    if (m_full_source_name) {
        free(m_full_source_name);
        m_full_source_name = NULL;
    }
    if (m_backup_name) {
        free(m_backup_name);
        m_backup_name = NULL;
    }
}

///////////////////////////////////////////////////////////////////////////////
//
void file_description::prepare_for_backup(const char *name)
{
    // TODO: strdup this string, then free it later.
    m_backup_name = strdup(name);
    m_in_source_dir = true;
}

///////////////////////////////////////////////////////////////////////////////
//
void file_description::set_full_source_name(const char *name)
{
    // TODO: strdup this string, then free it later.
    m_full_source_name = strdup(name);
}

///////////////////////////////////////////////////////////////////////////////
//
const char * file_description::get_full_source_name(void)
{
    return m_full_source_name;
}


///////////////////////////////////////////////////////////////////////////////
//
void file_description::lock(void)
{
    pthread_mutex_lock(&m_mutex);
}

///////////////////////////////////////////////////////////////////////////////
//
void file_description::unlock(void)
{
    pthread_mutex_unlock(&m_mutex);
}

///////////////////////////////////////////////////////////////////////////////
//
// open() -
//
// Description: 
//
//     Calls the operating system's open() syscall for the current
// file description.  This also sets the file descriptor in the 
// destination/backup space for the backup copy of the original file.
//
// Notes:
//
//     Open assumes that the backup file exists.  Create assumes the 
// backup file does NOT exist.
//
int file_description::open(void)
{
    int r = 0;
    int fd = 0;
    fd = call_real_open(m_backup_name, O_WRONLY, 0777);
    if (fd < 0) {
        int error = errno;

        // For now, don't store the fd if they are opening a dir.
        // That is just for fsync'ing a dir, which we do not care about.
        if(error == EISDIR) {
            goto out;
        }

        if(error != ENOENT && error != EISDIR) {
            perror("ERROR: <CAPTURE> ");
            r = -1;
            goto out;
        }

        r = this->create();
    } else {
        this->m_fd_in_dest_space = fd;
    }
    
out:
    return r;
}

///////////////////////////////////////////////////////////////////////////////
//
// create() -
//
// Description: 
//
//     Calls the operating system's open() syscall with the create
// flag for the current file description.  This also sets the file 
// descriptor in the destination/backup space for the backup
// copy of the original file.
//
// Notes:
//
//     Open assumes that the backup file exists.  Create assumes the 
// backup file does NOT exist.
//
int file_description::create(void)
{
    int r = 0;
    // Create file that was just opened, this assumes the parent directories
    // exist.
    int fd = 0;
    fd = call_real_open(m_backup_name, O_CREAT | O_WRONLY, 0777);
    if (fd < 0) {
        int error = errno;
        if(error != EEXIST) {
            r = -1;
            goto out;
        }
        fd = call_real_open(m_backup_name, O_WRONLY, 0777);
        if (fd < 0) {
            perror("ERROR: <CAPTURE>: Couldn't open backup copy of recently opened file.");
            r = -1;
            goto out;
        }
    }

    this->m_fd_in_dest_space = fd;

out:
    return r;
}

///////////////////////////////////////////////////////////////////////////////
//
// close() -
//
// Description: 
//
//     ...
//
int file_description::close(void)
{
    int r = 0;
    if(!m_in_source_dir) {
        goto out;
    }
    
    if(m_fd_in_dest_space == DEST_FD_INIT) {
        goto out;
    }

    // TODO: Check refcount, if it's zero we REALLY have to close
    // the file.  Otherwise, if there are any references left, 
    // we can only decrement the refcount; other file descriptors
    // are still open in the main application.
    r = call_real_close(this->m_fd_in_dest_space);
    if (r != 0) {
        perror("Toku Hot Backup: close() of backup file failed."); 
        r = -1;
    }

out:    
    return r;
}

///////////////////////////////////////////////////////////////////////////////
//
// write() -
//
// Description: 
//
//     ...
//
void file_description::write(ssize_t written, const void *buf)
{
    if (written > 0) {
        off_t position = m_offset;
        m_offset += written;
    
        if (!m_in_source_dir) {
            /* nothing */
        } else if (m_fd_in_dest_space == DEST_FD_INIT) {
            // We can't write to the backup file if it hasn't been created yet.
            /* nothing */
        } else {
            ssize_t second_write_size = call_real_pwrite(m_fd_in_dest_space, buf, written, position);
            if(second_write_size != written) {
                // TODO: Find some way to abort the backup, since our write failed.
            }
        }
    }
}

void file_description::read(ssize_t nbyte) {    
    m_offset += nbyte;
}


///////////////////////////////////////////////////////////////////////////////
//
// seek() -
//
// Description: 
//
//     ...
//
void file_description::lseek(off_t new_offset) {
    m_offset = new_offset;
}

///////////////////////////////////////////////////////////////////////////////
//
// pwrite() -
//
// Description: 
//
//     ...
//
int file_description::pwrite(const void *buf, size_t nbyte, off_t offset)
{
    int r = 0;
    if(!m_in_source_dir) {
        goto out;
    }
    
    if(m_fd_in_dest_space == DEST_FD_INIT) {
        goto out;
    }
    
    r = call_real_pwrite(this->m_fd_in_dest_space, buf, nbyte, offset);
    if (r < 0) {
        perror("Toku Hot Backup: pwrite() to backup file failed:"); 
        r = -1;
    }

out:
    return r;
}

///////////////////////////////////////////////////////////////////////////////
//
// truncate() -
//
// Description: 
//
//     ...
//
int file_description::truncate(off_t length)
{
    int r = 0;
    if(!m_in_source_dir) {
        goto out;
    }

    if (m_fd_in_dest_space == DEST_FD_INIT) {
        goto out;
    }

    r = call_real_ftruncate(this->m_fd_in_dest_space, length);
    if (r < 0) {
        perror("Toku Hot Backup: truncating backup file failed:");
        r = -1;
    }

out:    
    return r;
}
