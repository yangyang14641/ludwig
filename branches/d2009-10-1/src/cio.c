/*****************************************************************************
 *
 *  cio.c
 *
 *  Colloid I/O, serial and parallel.
 *
 *  $Id: cio.c,v 1.7.16.2 2010-03-30 14:16:39 kevin Exp $
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *  (c) 2010 The University of Edinburgh
 *
 *****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pe.h"
#include "coords.h"
#include "colloids.h"
#include "cio.h"

#include "runtime.h"
#include "interaction.h"

enum io_type { BINARY, ASCII, ASCII_SERIAL };

/* struct for parallel IO */
typedef struct{
  int root;                      /* Root PE of current I/O group */
  int n_io;                      /* Number of parallel IO group */
  int size;                      /* Size (in PEs) of each IO group */
  int index;                     /* Index of current IO group */
  int rank;                      /* Rank of PE in IO group */
} IO_Param;

static MPI_Comm IO_Comm;
static IO_Param io_grp;

void CIO_read_list_ascii(FILE *);
void CIO_read_list_ascii_serial(FILE *);
void CIO_read_list_binary(FILE *);
void CIO_read_header_ascii(FILE *);
void CIO_read_header_binary(FILE *);
void CIO_write_header_ascii(FILE *);
void CIO_write_header_binary(FILE *);
void CIO_write_header_null(FILE *);
int  CIO_write_list_ascii(FILE *, int, int, int);
int  CIO_write_list_binary(FILE *, int, int, int);
int  CIO_write_xu_binary(FILE *, int, int, int);
void CIO_set_cio_format(int, int);

static void (* CIO_write_header)(FILE *);
static int  (* CIO_write_list)(FILE *, int, int, int);
static void (* CIO_read_header)(FILE *);
static void (* CIO_read_list)(FILE *);
static void CIO_count_colloids(void);
static void cio_filename(char * filename, const char * stub);

static int nlocal_;                       /* Local number of colloids. */
static int ntotal_;

/*****************************************************************************
 *
 *  colloid_io_init
 *
 *****************************************************************************/

void colloid_io_init(void) {

  char tmp[256];
  int input_format;
  int output_format;

  io_grp.n_io = 1; /* Always 1 at moment */

  if (io_grp.n_io > pe_size()) io_grp.n_io = pe_size();
  io_grp.size = pe_size() / io_grp.n_io;

  if((cart_rank()%io_grp.size) == 0) {
    io_grp.root = 1;
  }
  else {
    io_grp.root = 0;
  }

  io_grp.index = cart_rank()/io_grp.size;

  /* Create communicator for each IO group, and get rank within IO group */
  MPI_Comm_split(cart_comm(), io_grp.index, cart_rank(), &IO_Comm);
  MPI_Comm_rank(IO_Comm, &io_grp.rank);

  input_format = BINARY;
  output_format = BINARY;

  RUN_get_string_parameter("colloid_io_format_input", tmp, 256);
  if (strncmp("ASCII",  tmp, 5) == 0 ) input_format = ASCII;
  if (strncmp("ASCII_SERIAL",  tmp, 12) == 0 ) input_format = ASCII_SERIAL;
  if (strncmp("BINARY", tmp, 6) == 0 ) input_format = BINARY;

  RUN_get_string_parameter("colloid_io_format_output", tmp, 256);
  if (strncmp("ASCII",  tmp, 5) == 0 ) output_format = ASCII;
  if (strncmp("BINARY", tmp, 6) == 0 ) output_format = BINARY;
  CIO_set_cio_format(input_format, output_format);

  return;
}

/*****************************************************************************
 *
 *  colloid_io_finish
 *
 *****************************************************************************/

void colloid_io_finish(void) {

  return;
}

/*****************************************************************************
 *
 *  CIO_count_colloids
 *
 *  Count the local number of (physical, not halo) colloids and
 *  add up across proceses.
 *
 *****************************************************************************/

void CIO_count_colloids() {

  nlocal_ = colloid_nlocal();

  MPI_Allreduce(&nlocal_, &ntotal_, 1, MPI_INT, MPI_SUM, cart_comm());

  assert(ntotal_ == colloid_ntotal());

  return;
}

/*****************************************************************************
 *
 *  colloid_io_write
 *
 *  Write information on the colloids in the local domain proper (not
 *  including the halos) to the specified file.
 *
 *  In parallel, one file per io group is used. Processes within a
 *  group write to the same file on reciept of the token.
 *
 *****************************************************************************/

void colloid_io_write(const char * filename) {

  FILE *    fp_state;
  char      filename_io[FILENAME_MAX];
  int       token = 0;
  int       ic, jc, kc;

  const int tag = 9572;

  MPI_Status status;

  if (colloid_ntotal() == 0) return;

  /* Set the filename */

  cio_filename(filename_io, filename);

  info("colloid_io_write:\n");
  info("writing colloid information to %s etc\n", filename_io);

  /* Make sure everyone has their current number of particles
   * up-to-date */

  CIO_count_colloids();

  if (io_grp.root) {
    /* Open the file, and write the header, followed by own colloids.
     * When this is done, pass the token to the next processs in the
     * group. */

    fp_state = fopen(filename_io, "w+b");
    if (fp_state == NULL) fatal("Failed to open %s\n", filename_io);
  }
  else {
    /* Non-io-root process. Block until we receive the token from the
     * previous process, after which we can go ahead and append our own
     * colloids to the file. */

    MPI_Recv(&token, 1, MPI_INT, io_grp.rank - 1, tag, IO_Comm, &status);
    fp_state = fopen(filename_io, "a+b");
    if (fp_state == NULL) fatal("Failed to open %s\n", filename_io);
  }

  /* Write the local colloid state, consisting of header plus data. */

  CIO_write_header(fp_state);

  for (ic = 1; ic <= Ncell(X); ic++) {
    for (jc = 1; jc <= Ncell(Y); jc++) {
      for (kc = 1; kc <= Ncell(Z); kc++) {
	token += CIO_write_list(fp_state, ic, jc, kc);
      }
    }
  }

  fclose(fp_state);

  /* This process has finished, so we can pass the token to the
   * next process in the group. If this is the last process in the
   * group, we've completed the file. */

  if (io_grp.rank < io_grp.size - 1) {
    /* Send the token, which happens to be the accumulated number of
     * particles. */

    MPI_Ssend(&token, 1, MPI_INT, io_grp.rank + 1, tag, IO_Comm);
  }

  return;
}


/*****************************************************************************
 *
 *  colloid_io_read
 *
 *  This is the driver routine to read colloid information from file.
 *
 *****************************************************************************/

void colloid_io_read(const char * filename) {

  FILE *    fp_state;
  char      filename_io[FILENAME_MAX];
  long int  token = 0;
  const int tag = 9573;

  MPI_Status status;

  /* Set the filename from the stub and the extension */

  info("colloid_io_read:\n");
  cio_filename(filename_io, filename);

  if (io_grp.root) {
    /* Open the file and read the header information */
    fp_state = fopen(filename_io, "r");
    if (fp_state == NULL) fatal("Failed to open %s\n", filename_io);
  }
  else {
    /* Block until we receive the token which allows us to proceed.
     * Then, open the file and move to the appropriate position before
     * starting to read. */

    MPI_Recv(&token, 1, MPI_LONG, io_grp.rank - 1, tag, IO_Comm, &status);

    fp_state = fopen(filename_io, "r");
    if (fp_state == NULL) fatal("Failed to open %s\n", filename_io);
    rewind(fp_state);
    /* This is a fix for reading serial files in parallel */
    if (CIO_read_list != CIO_read_list_ascii_serial) {
      fseek(fp_state, token, SEEK_SET);
    }
  }

  /* Read the data */

  CIO_read_header(fp_state);
  CIO_read_list(fp_state);

  token = ftell(fp_state);
  fclose(fp_state);

  /* Pass on the token, which is the current offset in the file */

  if (io_grp.rank < io_grp.size - 1) {

    MPI_Ssend(&token, 1, MPI_LONG, io_grp.rank + 1, tag, IO_Comm);

  }

  /* This is set here, as the total is not yet known. */
  set_N_colloid(ntotal_);
  info("Reading information for %d particles\n", ntotal_);

  return;
}


/*****************************************************************************
 *
 *  CIO_write_header_ascii
 *
 *  Write colloid information to file in human-readable form.
 *
 *****************************************************************************/

void CIO_write_header_ascii(FILE * fp) {

  fprintf(fp, "I/O n_io:  %22d\n", io_grp.n_io);
  fprintf(fp, "I/O index: %22d\n", io_grp.index);
  fprintf(fp, "N_colloid: %22d\n", ntotal_);
  fprintf(fp, "nlocal:    %22d\n", nlocal_);

  return;
}


/*****************************************************************************
 *
 *  CIO_read_header_ascii
 *
 *  Everybody reads their own header, which should include the
 *  local number of particles.
 *
 *  Issues
 *    A large nuumber of these values are ignored at the moment,
 *    meaning that run time input must agree. There is an outstanding
 *    issue as to what to do if one wants to override run time input.
 *
 *****************************************************************************/

void CIO_read_header_ascii(FILE * fp) {

  int    read_int;

  info("Colloid file header information has been filtered\n");

  fscanf(fp, "I/O n_io:  %22d\n",  &read_int);
  fscanf(fp, "I/O index: %22d\n",  &read_int);
  fscanf(fp, "N_colloid: %22d\n",  &ntotal_);
  fscanf(fp, "nlocal:    %22d\n",  &nlocal_);

  return;
}


/*****************************************************************************
 *
 *  CIO_write_header_binary
 *
 *  Write the colloid header information to file.
 *
 *****************************************************************************/

void CIO_write_header_binary(FILE * fp) {

  fwrite(&(io_grp.n_io),   sizeof(int),     1, fp);
  fwrite(&(io_grp.index),  sizeof(int),     1, fp);
  fwrite(&ntotal_,         sizeof(int),     1, fp);
  fwrite(&nlocal_,         sizeof(int),     1, fp);

  return;
}

void CIO_write_header_null(FILE * fp) {

  fwrite(&nlocal_, sizeof(int), 1, fp);

  return;
}

/*****************************************************************************
 *
 *  CIO_read_header_binary
 *
 *  The read equivalent of the above.
 *
 *****************************************************************************/

void CIO_read_header_binary(FILE * fp) {

  int     read_int;

  fread(&(read_int), sizeof(int),     1, fp); /* n_io */
  fread(&(read_int), sizeof(int),     1, fp);
  fread(&ntotal_,    sizeof(int),     1, fp);
  fread(&nlocal_,    sizeof(int),     1, fp);

  return;
}

/*****************************************************************************
 *
 *  CIO_write_list_ascii
 *
 *****************************************************************************/

int CIO_write_list_ascii(FILE * fp, int ic, int jc, int kc) {

  Colloid * p_colloid;
  int       nwrite = 0;

  p_colloid = CELL_get_head_of_list(ic, jc, kc);

  while (p_colloid) {
    nwrite++;

    fprintf(fp, "%22.15e %22.15e %d\n", p_colloid->a0, p_colloid->ah,
	    p_colloid->index);
    fprintf(fp, "%22.15e %22.15e %22.15e\n", p_colloid->r.x, p_colloid->r.y,
	    p_colloid->r.z);
    fprintf(fp, "%22.15e %22.15e %22.15e\n", p_colloid->v.x, p_colloid->v.y,
	    p_colloid->v.z);
    fprintf(fp, "%22.15e %22.15e %22.15e\n", p_colloid->omega.x,
	    p_colloid->omega.y, p_colloid->omega.z);
    fprintf(fp, "%22.15e %22.15e %22.15e\n", p_colloid->s[X], p_colloid->s[Y],
            p_colloid->s[Z]);
    fprintf(fp, "%22.15e\n", p_colloid->deltaphi);
 
    /* Next colloid */
    p_colloid = p_colloid->next;
  }

  return nwrite;
}


/*****************************************************************************
 *
 *  CIO_read_ascii
 *
 *  The colloid data from file an add a new particle to the local
 *  list.
 *
 *****************************************************************************/

void CIO_read_list_ascii(FILE * fp) {

  int       nread;
  int       read_index;
  double    read_a0;
  double    read_ah;
  FVector   read_r, read_v, read_o;
  double    read_s[3];
  double    read_deltaphi;
  Colloid * p_colloid;

  for (nread = 0; nread < nlocal_; nread++) {

    fscanf(fp, "%22le %22le %22d\n",  &(read_a0), &(read_ah), &(read_index));
    fscanf(fp, "%22le %22le %22le\n", &(read_r.x), &(read_r.y), &(read_r.z));
    fscanf(fp, "%22le %22le %22le\n", &(read_v.x), &(read_v.y), &(read_v.z));
    fscanf(fp, "%22le %22le %22le\n", &(read_o.x), &(read_o.y), &(read_o.z));
    fscanf(fp, "%22le %22le %22le\n", read_s, read_s+1, read_s+2);
    fscanf(fp, "%22le\n",             &(read_deltaphi));

    p_colloid = COLL_add_colloid(read_index, read_a0, read_ah, read_r,
				 read_v, read_o);

    if (p_colloid) {
      p_colloid->deltaphi = read_deltaphi;
      p_colloid->s[X] = read_s[X];
      p_colloid->s[Y] = read_s[Y];
      p_colloid->s[Z] = read_s[Z];
    }
    else {
      /* This didn't go into the cell list */
      fatal("Colloid information doesn't tally with read position\n");
    }
  }

  return;
}

/*****************************************************************************
 *
 *  CIO_read_list_ascii_serial
 *
 *  This is a solution to reading a serial file in parallel.
 *  Each process reads all the particles, and throws away
 *  those not in the local domain.
 *
 *****************************************************************************/

void CIO_read_list_ascii_serial(FILE * fp) {

  int       nread;
  int       read_index;
  double    read_a0;
  double    read_ah;
  FVector   read_r, read_v, read_o;
  double    read_s[3];
  double    read_deltaphi;
  Colloid * p_colloid;

  for (nread = 0; nread < ntotal_; nread++) {

    fscanf(fp, "%22le %22le %22d\n",  &(read_a0), &(read_ah), &(read_index));
    fscanf(fp, "%22le %22le %22le\n", &(read_r.x), &(read_r.y), &(read_r.z));
    fscanf(fp, "%22le %22le %22le\n", &(read_v.x), &(read_v.y), &(read_v.z));
    fscanf(fp, "%22le %22le %22le\n", &(read_o.x), &(read_o.y), &(read_o.z));
    fscanf(fp, "%22le %22le %22le\n", read_s, read_s+1, read_s+2);
    fscanf(fp, "%22le\n",             &(read_deltaphi));

    p_colloid = COLL_add_colloid_no_halo(read_index, read_a0, read_ah, read_r,
					 read_v, read_o);

    if (p_colloid) {
      p_colloid->deltaphi = read_deltaphi;
      p_colloid->s[X] = read_s[X];
      p_colloid->s[Y] = read_s[Y];
      p_colloid->s[Z] = read_s[Z];
    }
  }

  return;
}

/*****************************************************************************
 *
 *  CIO_write_list_binary
 *
 *  Write out the colliod information (if any) for the specified list.
 *
 *****************************************************************************/

int CIO_write_list_binary(FILE * fp, int ic, int jc, int kc) {

  Colloid * p_colloid;
  int       nwrite = 0;

  p_colloid = CELL_get_head_of_list(ic, jc, kc);

  while (p_colloid) {
    nwrite++;
    fwrite(&(p_colloid->a0),       sizeof(double),  1, fp);
    fwrite(&(p_colloid->ah),       sizeof(double),  1, fp);
    fwrite(&(p_colloid->index),    sizeof(int),     1, fp);
    fwrite(&(p_colloid->r),        sizeof(FVector), 1, fp);
    fwrite(&(p_colloid->v),        sizeof(FVector), 1, fp);
    fwrite(&(p_colloid->omega),    sizeof(FVector), 1, fp);
    fwrite(p_colloid->dr,          sizeof(double),  3, fp);
    fwrite(p_colloid->s,           sizeof(double),  3, fp);
    fwrite(&(p_colloid->deltaphi), sizeof(double),  1, fp);

    /* Next colloid */
    p_colloid = p_colloid->next;
  }

  return nwrite;
}


/*****************************************************************************
 *
 *  CIO_read_binary
 *
 *  Write me.
 *
 *****************************************************************************/

void CIO_read_list_binary(FILE * fp) {

  int       nread;
  int       read_index;
  double    read_a0;
  double    read_ah;
  FVector   read_r, read_v, read_o;
  double    read_dr[3];
  double    read_s[3];
  double    read_deltaphi;
  Colloid * p_colloid;

  for (nread = 0; nread < nlocal_; nread++) {

    fread(&read_a0,       sizeof(double),  1, fp);
    fread(&read_ah,       sizeof(double),  1, fp);
    fread(&read_index,    sizeof(int),     1, fp);
    fread(&read_r,        sizeof(FVector), 1, fp);
    fread(&read_v,        sizeof(FVector), 1, fp);
    fread(&read_o,        sizeof(FVector), 1, fp);
    fread(read_dr,        sizeof(double),  3, fp);
    fread(read_s,         sizeof(double),  3, fp);
    fread(&read_deltaphi, sizeof(double),  1, fp);

    p_colloid = COLL_add_colloid(read_index, read_a0, read_ah, read_r,
				 read_v, read_o);

    if (p_colloid) {
      int i;
      p_colloid->deltaphi = read_deltaphi;
      for (i = 0; i < 3; i++) {
	p_colloid->dr[i] = read_dr[i];
	p_colloid->s[i] = read_s[i];
      }
    }
    else {
      /* This didn't go into the cell list */
      fatal("Colloid information doesn't tally with read position\n");
    }
  }

  return;
}

/*****************************************************************************
 *
 *  CIO_write_xu_binary
 *
 *  Write the position and velocity alone.
 *
 *****************************************************************************/

int CIO_write_xu_binary(FILE * fp, int ic, int jc, int kc) {

  Colloid * p_colloid;
  int       nwrite = 0;

  p_colloid = CELL_get_head_of_list(ic, jc, kc);

  while (p_colloid) {
    nwrite++;
    fwrite(&(p_colloid->index),    sizeof(int),     1, fp);
    fwrite(&(p_colloid->r),        sizeof(FVector), 1, fp);
    fwrite(&(p_colloid->v),        sizeof(FVector), 1, fp);

    /* Next colloid */
    p_colloid = p_colloid->next;
  }

  return nwrite;
}


/*****************************************************************************
 *
 *  CIO_set_cio_format
 *
 *  Set the format for IO {BINARY|ASCII|ASCII_SERIAL}.
 *
 *****************************************************************************/

void CIO_set_cio_format(int io_intype, int io_outtype) {

  switch (io_intype) {
  case BINARY:
    CIO_read_header  = CIO_read_header_binary;
    CIO_read_list    = CIO_read_list_binary;
    break;
  case ASCII:
    CIO_read_header  = CIO_read_header_ascii;
    CIO_read_list    = CIO_read_list_ascii;
    break;
  case ASCII_SERIAL:
    CIO_read_header  = CIO_read_header_ascii;
    CIO_read_list    = CIO_read_list_ascii_serial;
    break;
  default:
    fatal("Invalid colloid input format (value %d)\n", io_intype);
  }

  switch (io_outtype) {
  case BINARY:
    CIO_write_header = CIO_write_header_binary;
    CIO_write_list   = CIO_write_list_binary;
    break;
  case ASCII:
    CIO_write_header = CIO_write_header_ascii;
    CIO_write_list   = CIO_write_list_ascii;
    break;
  default:
    fatal("Invalid colloid output format (value %d)\n", io_outtype);
  }

  return;
}

/*****************************************************************************
 *
 *  cio_filename
 *
 *  Add the extension to the supplied stub.
 *
 *****************************************************************************/

static void cio_filename(char * filename, const char * stub) {

  assert(stub);
  assert(strlen(stub) < FILENAME_MAX/2);  /* Check stub not too long */

  if (io_grp.index >= 1000) fatal("Format botch for cio stub %s\n", stub);

  sprintf(filename, "%s.%3.3d-%3.3d", stub, io_grp.n_io, io_grp.index + 1);

  return;
}
