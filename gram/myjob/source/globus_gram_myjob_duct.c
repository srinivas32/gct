
#include <assert.h>
#include <stdlib.h>

#include "globus_gram_myjob.h"

#include "globus_common.h"
#include "nexus.h"
#include "globus_duct_runtime.h"

/* make sure this is consistent with that in gram-job-manager... */
#define DUCT_CONTACT "GLOBUS_GRAM_MYJOB_DUCT_CONTACT"

static globus_duct_runtime_t s_duct;
static globus_fifo_t s_incoming_msgs;
static nexus_cond_t  s_cond;
static nexus_mutex_t s_mutex;

typedef struct globus_gram_myjob_msg_s {
  int len;
  globus_byte_t *msg;
} globus_gram_myjob_msg_t;


static
s_incoming_msg_handler (globus_duct_runtime_t * runtimep,
			int                     size,
			globus_byte_t         * data,
			void                  * user_data)
{
  int err;
  globus_gram_myjob_msg_t *msg;

  msg = globus_malloc (sizeof (globus_gram_myjob_msg_t));
  assert (msg!=NULL);

  msg->size = size;
  msg->msg = data;

  err = nexus_mutex_lock (&s_mutex);
  assert (!err);

  err = globus_fifo_enqueue (&s_incoming_msgs,
			     (void *) msg);
  assert (!err);

  nexus_cond_broadcast (&s_cond);
  err = nexus_mutex_unlock (&s_mutex);
}

static int
s_myjob_init ()
{
  int err;
  char * duct_contact;

  err = globus_fifo_init (&s_incoming_msgs);
  assert (!err);

  err = nexus_cond_init (&s_cond, NULL);
  assert (!err);

  err = nexus_mutex_init (&s_mutex, NULL);
  assert (!err);

  duct_contact = getenv (DUCT_CONTACT);

  err = globus_duct_runtime_init (&s_duct,
				  duct_contact,
				  0,
				  s_incoming_msg_handler,
				  NULL,
				  NULL,
				  NULL);
  assert (!err);

  return GLOBUS_SUCCESS;
}

static void
s_myjob_reset ()
{
  int err;

  err = nexus_mutex_lock (&s_mutex);
  assert (!err);

  while (! globus_fifo_empty (&s_incoming_msgs) ) {
    globus_gram_myjob_msg_t * msgp;

    msgp = ((globus_gram_myjob_msg_t *)
	    globus_fifo_dequeue (&s_incoming_msg));

    assert (msgp!=NULL);

    globus_free (msgp->msg);
    globus_free (msgp);
  }

  err = nexus_mutex_unlock (&s_mutex);
  assert (!err);
}

static void 
s_myjob_done ()
{
  int err;

  err = globus_fifo_destroy (&s_incoming_msgs);
  assert (!err);

  err = globus_duct_runtime_destroy (&s_duct);
  assert (!err);

  nexus_mutex_destroy (&s_mutex);
  nexus_cond_destroy (&s_cond);
}


static int s_myjob_module_enabled = 0;

static int
s_myjob_activate ()
{
  if ( globus_module_activate (GLOBUS_COMMON_MODULE) != GLOBUS_SUCCESS ) 
    goto activate_common_module_error;

  if ( globus_module_activate (GLOBUS_NEXUS_MODULE) != GLOBUS_SUCCESS ) 
    goto activate_nexus_module_error;

  if ( globus_module_activate (GLOBUS_DUCT_RUNTIME_MODULE) != GLOBUS_SUCCESS )
    goto activate_duct_runtime_module_error;

  s_myjob_init ();

  return GLOBUS_SUCCESS;


activate_duct_runtime_module_error:
  globus_module_deativate (GLOBUS_NEXUS_MODULE);

activate_nexus_module_error:
  globus_module_deactivate (GLOBUS_COMMON_MODULE);

activate_common_module_error:
  return GLOBUS_FAILURE;
}


int 
globus_gram_myjob_activate ()
{
  if ( s_myjob_module_enabled == 0 ) {
    if ( s_myjob_activate () == GLOBUS_SUCCESS ) {
      s_myjob_module_enabled = 1;
      return GLOBUS_SUCCESS;
    }
    else
      return GLOBUS_FAILURE;
  }
  else
    return GLOBUS_SUCCESS;
}


static int
s_myjob_deactivate ()
{
  int rc;

  rc = GLOBUS_SUCCESS;

  if ( globus_module_deactivate (GLOBUS_DUCT_RUNTIME_MODULE) != GLOBUS_SUCCESS)
    rc = GLOBUS_FAILURE;

  if ( globus_module_deactivate (GLOBUS_NEXUS_MODULE) != GLOBUS_SUCCESS)
    rc = GLOBUS_FAILURE;

  if ( globus_module_deactivate (GLOBUS_COMMON_MODULE) != GLOBUS_SUCCESS )
    rc = GLOBUS_FAILURE;

  s_myjob_done ();

  return rc;
}


int
globus_gram_myjob_deactivate ()
{
  s_myjob_reset ();

  return GLOBUS_SUCCESS;
}


void 
globus_gram_myjob_atexit ()
{
  s_myjob_deactivate ();
}


int
globus_gram_myjob_rank (int * rankp)
{
  int err;
  int i;
  int remote_count;
  int local_addr;
  int * remote_addrs;
  globus_list_t *addrs_list;
  globus_list_t *sorted_addrss_list;
  globus_list_t *list_iter;

  if ( rankp == NULL ) 
    return GLOBUS_GRAM_MYJOB_ERROR_INVALID_PARAMETER;

  err = globus_duct_runtime_structure (&s_duct,
				       &local_addr,
				       &remote_count,
				       &remote_addrs);
  assert (!err);

  addrs_list = NULL;
  
  err = globus_list_insert (&addrs_list, (void *) local_addr);
  assert (!err);

  for (i=0; i<remote_count; i++) {
    err = globus_list_insert (&addrs_list, (void *) remote_addrs[i]);
    assert (!err);
  }

  globus_free (remote_addrs);

  sorted_addrs_list = globus_list_sort (addrs_list,
					globus_list_int_less,
					NULL);

  globus_list_free (addrs_list);
  addrs_list = NULL;

  list_iter = sorted_addrs_list;
  
  (*rankp) = -1;

  for (i=0; i<(remote_count+1); i++) {
    if ( ((int) globus_list_first (list_iter))
	 == local_addr ) 
      (*rankp) = i;
    list_iter = globus_list_rest (list_iter);
  }

  assert ( (*rankp) >= 0 );

  globus_list_free (sorted_addrs_list);

  return GLOBUS_SUCCESS;
}


int
globus_gram_myjob_size (int * sizep)
{
  int err;
  int remote_count;
  int local_addr;
  int * remote_addrs;

  if ( sizep == NULL ) 
    return GLOBUS_GRAM_MYJOB_ERROR_INVALID_PARAMETER;

  err = globus_duct_runtime_structure (&s_duct,
				       &local_addr,
				       &remote_count,
				       &remote_addrs);
  assert (!err);

  globus_free (remote_addrs);
  
  (*sizep) = remote_count + 1;

  return GLOBUS_SUCCESS;
}


int
globus_gram_myjob_send (int             dest_addr,
			globus_byte_t * msg,
			int             msg_len)
{
  int err;
  int i;
  int size;
  int remote_count;
  int dest_duct_addr;
  int local_addr;
  int * remote_addrs;
  globus_list_t *addrs_list;
  globus_list_t *sorted_addrss_list;
  globus_list_t *list_iter;

  err = globus_gram_myjob_size (&size);
  assert (!err);

  if ( (msg == NULL) || (msg_len < 0)
       || (dest_addr > (size-1))
       || (dest_addr < 0) ) 
    return GLOBUS_GRAM_MYJOB_ERROR_INVALID_PARAMETER;

  err = globus_duct_runtime_structure (&s_duct,
				       &local_addr,
				       &remote_count,
				       &remote_addrs);
  assert (!err);

  addrs_list = NULL;
  
  err = globus_list_insert (&addrs_list, (void *) local_addr);
  assert (!err);

  for (i=0; i<remote_count; i++) {
    err = globus_list_insert (&addrs_list, (void *) remote_addrs[i]);
    assert (!err);
  }

  globus_free (remote_addrs);

  sorted_addrs_list = globus_list_sort (addrs_list,
					globus_list_int_less,
					NULL);

  globus_list_free (addrs_list);
  addrs_list = NULL;

  list_iter = sorted_addrs_list;
  
  for (i=0; i<dest_addr; i++) {
    list_iter = globus_list_rest (list_iter);
  }

  dest_duct_addr = ((int) globus_list_first (list_iter));

  err = globus_duct_runtime_send (&s_duct,
				  dest_duct_addr,
				  msg_len,
				  msg);

  globus_list_free (sorted_addrs_list);

  return err;
}


int
globus_gram_myjob_receive (globus_byte_t * msgp,
			   int           * msg_lenp)
{
  int err;
  globus_gram_myjob_msg_t *duct_msgp;

  if ( (msgp == NULL) || (msg_lenp == NULL) ) 
    return GLOBUS_GRAM_MYJOB_ERROR_INVALID_PARAMETER;

  err = nexus_mutex_lock (&s_mutex);
  assert (!err);

  while ( globus_fifo_empty (&s_incoming_msgs) ) {
    nexus_cond_wait (&s_cond, &s_mutex);
  }

  duct_msgp = ((globus_gram_myjob_msg_t *)
	       globus_fifo_dequeue (&s_incoming_msgs));
  assert (msgp!=NULL);

  (*msg_lenp) = duct_msgp->len;

  for (i=0; i<duct_msgp->len; i++) {
    msgp[i] = duct_msgp->msg[i];
  }

  globus_free (duct_msgp->msg);
  globus_free (duct_msgp);
  
  err = nexus_mutex_unlock (&s_mutex);
  assert (!err);

  return GLOBUS_SUCCESS;
}


