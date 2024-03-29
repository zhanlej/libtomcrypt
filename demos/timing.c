/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 */
#include <tomcrypt.h>

#include <sys/time.h>

#ifdef EXT_MATH_LIB
#include <mbedtls/bignum.h>
/* Round up the even multiple of size, size has to be a multiple of 2 */
#define ROUNDUP(v, size) (((v) + ((__typeof__(v))(size) - 1)) & \
			  ~((__typeof__(v))(size) - 1))

#ifdef __ASSEMBLER__
#define BIT32(nr)		(1 << (nr))
#define BIT64(nr)		(1 << (nr))
#define SHIFT_U32(v, shift)	((v) << (shift))
#define SHIFT_U64(v, shift)	((v) << (shift))
#else
#define BIT32(nr)		(UINT32_C(1) << (nr))
#define BIT64(nr)		(UINT64_C(1) << (nr))
#define SHIFT_U32(v, shift)	((uint32_t)(v) << (shift))
#define SHIFT_U64(v, shift)	((uint64_t)(v) << (shift))
#endif
#define BIT(nr)			BIT32(nr)

#define COMPILE_TIME_ASSERT(x) \
	do { \
		switch (0) { case 0: case ((x) ? 1: 0): default : break; } \
	} while (0)

static int init(void **a)
{
	mbedtls_mpi *bn = XCALLOC(1, sizeof(*bn));

	if (!bn)
		return CRYPT_MEM;

	mbedtls_mpi_init(bn);
	*a = bn;
	return CRYPT_OK;
}

static int copy(void *a, void *b)
{
	if (mbedtls_mpi_copy(b, a))
		return CRYPT_MEM;
	return CRYPT_OK;
}

static int init_copy(void **a, void *b)
{
	if (init(a) != CRYPT_OK) {
		return CRYPT_MEM;
	}
	return copy(b, *a);
}

static void deinit(void *a)
{
   LTC_ARGCHKVD(a != NULL);
	mbedtls_mpi_free((mbedtls_mpi *)a);
	XFREE(a);
}

static int neg(void *a, void *b)
{
	if (mbedtls_mpi_copy(b, a))
		return CRYPT_MEM;
	((mbedtls_mpi *)b)->s *= -1;
	return CRYPT_OK;
}

static int set_int(void *a, ltc_mp_digit b)
{
	uint32_t b32 = b;

	if (b32 != b)
		return CRYPT_INVALID_ARG;

	mbedtls_mpi_uint p = b32;
	mbedtls_mpi bn = { .s = 1, .n = 1, .p = &p };

	if (mbedtls_mpi_copy(a, &bn))
		return CRYPT_MEM;
	return CRYPT_OK;
}

static unsigned long get_int(void *a)
{
	mbedtls_mpi *bn = a;

	if (!bn->n)
		return 0;

	return bn->p[bn->n - 1];
}

static ltc_mp_digit get_digit(void *a, int n)
{
	mbedtls_mpi *bn = a;

	COMPILE_TIME_ASSERT(sizeof(ltc_mp_digit) >= sizeof(mbedtls_mpi_uint));

	if (n < 0 || (size_t)n >= bn->n)
		return 0;

	return bn->p[n];
}

static int get_digit_count(void *a)
{
	return ROUNDUP(mbedtls_mpi_size(a), sizeof(mbedtls_mpi_uint)) /
	       sizeof(mbedtls_mpi_uint);
}

static int compare(void *a, void *b)
{
	int ret = mbedtls_mpi_cmp_mpi(a, b);

	if (ret < 0)
		return LTC_MP_LT;

	if (ret > 0)
		return LTC_MP_GT;

	return LTC_MP_EQ;
}

static int compare_d(void *a, ltc_mp_digit b)
{
	unsigned long v = b;
	unsigned int shift = 31;
	uint32_t mask = BIT(shift) - 1;
	mbedtls_mpi bn;

	mbedtls_mpi_init(&bn);
	while (1) {
		mbedtls_mpi_add_int(&bn, &bn, v & mask);
		v >>= shift;
		if (!v)
			break;
		mbedtls_mpi_shift_l(&bn, shift);
	}

	int ret = compare(a, &bn);

	mbedtls_mpi_free(&bn);

	return ret;
}

static int count_bits(void *a)
{
	return mbedtls_mpi_bitlen(a);
}

static int count_lsb_bits(void *a)
{
	return mbedtls_mpi_lsb(a);
}


static int twoexpt(void *a, int n)
{
	if (mbedtls_mpi_set_bit(a, n, 1))
		return CRYPT_MEM;

	return CRYPT_OK;
}

/* ---- conversions ---- */

/* read ascii string */
static int read_radix(void *a, const char *b, int radix)
{
	int res = mbedtls_mpi_read_string(a, radix, b);

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;
	if (res)
		return CRYPT_ERROR;

	return CRYPT_OK;
}

/* write one */
static int write_radix(void *a, char *b, int radix)
{
	size_t ol = SIZE_MAX;
	int res = mbedtls_mpi_write_string(a, radix, b, ol, &ol);

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;
	if (res)
		return CRYPT_ERROR;

	return CRYPT_OK;
}

/* get size as unsigned char string */
static unsigned long unsigned_size(void *a)
{
	return mbedtls_mpi_size(a);
}

/* store */
static int unsigned_write(void *a, unsigned char *b)
{
	int res = mbedtls_mpi_write_binary(a, b, unsigned_size(a));

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;
	if (res)
		return CRYPT_ERROR;

	return CRYPT_OK;
}

/* read */
static int unsigned_read(void *a, unsigned char *b, unsigned long len)
{
	int res = mbedtls_mpi_read_binary(a, b, len);

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;
	if (res)
		return CRYPT_ERROR;

	return CRYPT_OK;
}

/* add */
static int add(void *a, void *b, void *c)
{
	if (mbedtls_mpi_add_mpi(c, a, b))
		return CRYPT_MEM;

	return CRYPT_OK;
}

static int addi(void *a, ltc_mp_digit b, void *c)
{
	uint32_t b32 = b;

	if (b32 != b)
		return CRYPT_INVALID_ARG;

	mbedtls_mpi_uint p = b32;
	mbedtls_mpi bn = { .s = 1, .n = 1, .p = &p };

	return add(a, &bn, c);
}

/* sub */
static int sub(void *a, void *b, void *c)
{
	if (mbedtls_mpi_sub_mpi(c, a, b))
		return CRYPT_MEM;

	return CRYPT_OK;
}

static int subi(void *a, ltc_mp_digit b, void *c)
{
	uint32_t b32 = b;

	if (b32 != b)
		return CRYPT_INVALID_ARG;

	mbedtls_mpi_uint p = b32;
	mbedtls_mpi bn = { .s = 1, .n = 1, .p = &p };

	return sub(a, &bn, c);
}

/* mul */
static int mul(void *a, void *b, void *c)
{
	if (mbedtls_mpi_mul_mpi(c, a, b))
		return CRYPT_MEM;

	return CRYPT_OK;
}

static int muli(void *a, ltc_mp_digit b, void *c)
{
	if (b > (unsigned long) UINT32_MAX)
		return CRYPT_INVALID_ARG;

	if (mbedtls_mpi_mul_int(c, a, b))
		return CRYPT_MEM;

	return CRYPT_OK;
}

/* sqr */
static int sqr(void *a, void *b)
{
	return mul(a, a, b);
}

/* div */
static int divide(void *a, void *b, void *c, void *d)
{
	int res = mbedtls_mpi_div_mpi(c, d, a, b);

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;
	if (res)
		return CRYPT_ERROR;

	return CRYPT_OK;
}

static int div_2(void *a, void *b)
{
	if (mbedtls_mpi_copy(b, a))
		return CRYPT_MEM;

	if (mbedtls_mpi_shift_r(b, 1))
		return CRYPT_MEM;

	return CRYPT_OK;
}

/* modi */
static int modi(void *a, ltc_mp_digit b, ltc_mp_digit *c)
{
	mbedtls_mpi bn_b;
	mbedtls_mpi bn_c;
	int res = 0;

	mbedtls_mpi_init(&bn_b);
	mbedtls_mpi_init(&bn_c);

	res = set_int(&bn_b, b);
	if (res)
		return res;

	res = mbedtls_mpi_mod_mpi(&bn_c, &bn_b, a);
	if (!res)
		*c = get_int(&bn_c);

	mbedtls_mpi_free(&bn_b);
	mbedtls_mpi_free(&bn_c);

	if (res)
		return CRYPT_MEM;

	return CRYPT_OK;
}

/* gcd */
static int gcd(void *a, void *b, void *c)
{
	if (mbedtls_mpi_gcd(c, a, b))
		return CRYPT_MEM;

	return CRYPT_OK;
}

/* lcm */
static int lcm(void *a, void *b, void *c)
{
	int res = CRYPT_MEM;
	mbedtls_mpi tmp;

	mbedtls_mpi_init(&tmp);
	if (mbedtls_mpi_mul_mpi(&tmp, a, b))
		goto out;

	if (mbedtls_mpi_gcd(c, a, b))
		goto out;

	/* We use the following equality: gcd(a, b) * lcm(a, b) = a * b */
	res = divide(&tmp, c, c, NULL);
out:
	mbedtls_mpi_free(&tmp);
	return res;
}

static int mod(void *a, void *b, void *c)
{
	int res = mbedtls_mpi_mod_mpi(c, a, b);

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;
	if (res)
		return CRYPT_ERROR;

	return CRYPT_OK;
}

static int addmod(void *a, void *b, void *c, void *d)
{
	int res = add(a, b, d);

	if (res)
		return res;

	return mod(d, c, d);
}

static int submod(void *a, void *b, void *c, void *d)
{
	int res = sub(a, b, d);

	if (res)
		return res;

	return mod(d, c, d);
}

static int mulmod(void *a, void *b, void *c, void *d)
{
	int res;
	mbedtls_mpi ta;
	mbedtls_mpi tb;

	mbedtls_mpi_init(&ta);
	mbedtls_mpi_init(&tb);

	res = mod(a, c, &ta);
	if (res)
		goto out;
	res = mod(b, c, &tb);
	if (res)
		goto out;
	res = mul(&ta, &tb, d);
	if (res)
		goto out;
	res = mod(d, c, d);
out:
	mbedtls_mpi_free(&ta);
	mbedtls_mpi_free(&tb);
	return res;
}

static int sqrmod(void *a, void *b, void *c)
{
	return mulmod(a, a, b, c);
}

/* invmod */
static int invmod(void *a, void *b, void *c)
{
	int res = mbedtls_mpi_inv_mod(c, a, b);

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;
	if (res)
		return CRYPT_ERROR;

	return CRYPT_OK;
}


/* setup */
static int montgomery_setup(void *a, void **b)
{
	*b = malloc(sizeof(mbedtls_mpi_uint));
	if (!*b)
		return CRYPT_MEM;

	mpi_montg_init(*b, a);

	return CRYPT_OK;
}

/* get normalization value */
static int montgomery_normalization(void *a, void *b)
{
	size_t c = ROUNDUP(mbedtls_mpi_size(b), sizeof(mbedtls_mpi_uint)) * 8;

	if (mbedtls_mpi_lset(a, 1))
		return CRYPT_MEM;
	if (mbedtls_mpi_shift_l(a, c))
		return CRYPT_MEM;
	if (mbedtls_mpi_mod_mpi(a, a, b))
		return CRYPT_MEM;

	return CRYPT_OK;
}

/* reduce */
static int montgomery_reduce(void *a, void *b, void *c)
{
	mbedtls_mpi A;
	mbedtls_mpi *N = b;
	mbedtls_mpi_uint *mm = c;
	mbedtls_mpi T;
	int ret = CRYPT_MEM;

	mbedtls_mpi_init(&T);
	mbedtls_mpi_init(&A);

	if (mbedtls_mpi_grow(&T, (N->n + 1) * 2))
		goto out;

	if (mbedtls_mpi_cmp_mpi(a, N) > 0) {
		if (mbedtls_mpi_mod_mpi(&A, a, N))
			goto out;
	} else {
		if (mbedtls_mpi_copy(&A, a))
			goto out;
	}

	if (mbedtls_mpi_grow(&A, N->n + 1))
		goto out;

	if (mpi_montred(&A, N, *mm, &T))
		goto out;

	if (mbedtls_mpi_copy(a, &A))
		goto out;

	ret = CRYPT_OK;
out:
	mbedtls_mpi_free(&A);
	mbedtls_mpi_free(&T);

	return ret;
}

/* clean up */
static void montgomery_deinit(void *a)
{
	free(a);
}

/*
 * This function calculates:
 *  d = a^b mod c
 *
 * @a: base
 * @b: exponent
 * @c: modulus
 * @d: destination
 */
static int exptmod(void *a, void *b, void *c, void *d)
{
	int res;

	if (d == a || d == b || d == c) {
		mbedtls_mpi dest;

		mbedtls_mpi_init(&dest);
		res = mbedtls_mpi_exp_mod(&dest, a, b, c, NULL);
		if (!res)
			res = mbedtls_mpi_copy(d, &dest);
		mbedtls_mpi_free(&dest);
	} else {
		res = mbedtls_mpi_exp_mod(d, a, b, c, NULL);
	}

	if (res)
		return CRYPT_MEM;
	else
		return CRYPT_OK;
}

static int rng_read(void *ignored, unsigned char *buf, size_t blen)
{
   (void) ignored;
	// if (crypto_rng_read(buf, blen))
	if (sprng_read(buf, blen, NULL) == 0)
		return MBEDTLS_ERR_MPI_FILE_IO_ERROR;
	return 0;
}

static int isprime(void *a, int b, int *c)
{
   (void) b;
	int res = mbedtls_mpi_is_prime(a, rng_read, NULL);

	if (res == MBEDTLS_ERR_MPI_ALLOC_FAILED)
		return CRYPT_MEM;

	if (res)
		*c = LTC_MP_NO;
	else
		*c = LTC_MP_YES;

	return CRYPT_OK;
}

static int mpa_rand(void *a, int size)
{
	if (mbedtls_mpi_fill_random(a, size, rng_read, NULL))
		return CRYPT_MEM;

	return CRYPT_OK;
}

ltc_math_descriptor mbedtls_mpi_desc = {
	.name = "MPI",
	.bits_per_digit = sizeof(mbedtls_mpi_uint) * 8,

	.init = &init,
	// .init_size = &init_size,
	.init_copy = &init_copy,
	.deinit = &deinit,

	.neg = &neg,
	.copy = &copy,

	.set_int = &set_int,
	.get_int = &get_int,
	.get_digit = &get_digit,
	.get_digit_count = &get_digit_count,
	.compare = &compare,
	.compare_d = &compare_d,
	.count_bits = &count_bits,
	.count_lsb_bits = &count_lsb_bits,
	.twoexpt = &twoexpt,

	.read_radix = &read_radix,
	.write_radix = &write_radix,
	.unsigned_size = &unsigned_size,
	.unsigned_write = &unsigned_write,
	.unsigned_read = &unsigned_read,

	.add = &add,
	.addi = &addi,
	.sub = &sub,
	.subi = &subi,
	.mul = &mul,
	.muli = &muli,
	.sqr = &sqr,
	.mpdiv = &divide,
	.div_2 = &div_2,
	.modi = &modi,
	.gcd = &gcd,
	.lcm = &lcm,

	.mulmod = &mulmod,
	.sqrmod = &sqrmod,
	.invmod = &invmod,

	.montgomery_setup = &montgomery_setup,
	.montgomery_normalization = &montgomery_normalization,
	.montgomery_reduce = &montgomery_reduce,
	.montgomery_deinit = &montgomery_deinit,

	.exptmod = &exptmod,
	.isprime = &isprime,

#ifdef LTC_MECC
#ifdef LTC_MECC_FP
	.ecc_ptmul = &ltc_ecc_fp_mulmod,
#else
	.ecc_ptmul = &ltc_ecc_mulmod,
#endif /* LTC_MECC_FP */
	.ecc_ptadd = &ltc_ecc_projective_add_point,
	.ecc_ptdbl = &ltc_ecc_projective_dbl_point,
	.ecc_map = &ltc_ecc_map,
#ifdef LTC_ECC_SHAMIR
#ifdef LTC_MECC_FP
	.ecc_mul2add = &ltc_ecc_fp_mul2add,
#else
	.ecc_mul2add = &ltc_ecc_mul2add,
#endif /* LTC_MECC_FP */
#endif /* LTC_ECC_SHAMIR */
#endif /* LTC_MECC */

#ifdef LTC_MRSA
	.rsa_keygen = &rsa_make_key,
	.rsa_me = &rsa_exptmod,
#endif
	.addmod = addmod,
	.submod = submod,
	.rand = &mpa_rand,

};
#endif

#if defined(_WIN32)
   #define PRI64  "I64d"
#else
   #define PRI64  "ll"
#endif

static prng_state yarrow_prng;

/* timing */
#define KTIMES  25
#define TIMES   100000

static struct list {
    int id;
    ulong64 spd1, spd2, avg;
} results[100];
static int no_results;

static int sorter(const void *a, const void *b)
{
   const struct list *A, *B;
   A = a;
   B = b;
   if (A->avg < B->avg) return -1;
   if (A->avg > B->avg) return 1;
   return 0;
}

static void tally_results(int type)
{
   int x;

   /* qsort the results */
   qsort(results, no_results, sizeof(struct list), &sorter);

   fprintf(stderr, "\n");
   if (type == 0) {
      for (x = 0; x < no_results; x++) {
         fprintf(stderr, "%-20s: Schedule at %6lu\n", cipher_descriptor[results[x].id].name, (unsigned long)results[x].spd1);
      }
   } else if (type == 1) {
      for (x = 0; x < no_results; x++) {
        printf
          ("%-20s[%3d]: Encrypt at %5"PRI64"u, Decrypt at %5"PRI64"u\n", cipher_descriptor[results[x].id].name, cipher_descriptor[results[x].id].ID, results[x].spd1, results[x].spd2);
      }
   } else {
      for (x = 0; x < no_results; x++) {
        printf
          ("%-20s: Process at %5"PRI64"u\n", hash_descriptor[results[x].id].name, results[x].spd1 / 1000);
      }
   }
}

/* RDTSC from Scott Duplichan */
static ulong64 rdtsc (void)
   {
   #if defined __GNUC__ && !defined(LTC_NO_ASM)
      #if defined(__i386__) || defined(__x86_64__)
         /* version from http://www.mcs.anl.gov/~kazutomo/rdtsc.html
          * the old code always got a warning issued by gcc, clang did not complain...
          */
         unsigned hi, lo;
         __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
         return ((ulong64)lo)|( ((ulong64)hi)<<32);
      #elif defined(LTC_PPC32) || defined(TFM_PPC32)
         unsigned long a, b;
         __asm__ __volatile__ ("mftbu %1 \nmftb %0\n":"=r"(a), "=r"(b));
         return (((ulong64)b) << 32ULL) | ((ulong64)a);
      #elif defined(__ia64__)  /* gcc-IA64 version */
         unsigned long result;
         __asm__ __volatile__("mov %0=ar.itc" : "=r"(result) :: "memory");
         while (__builtin_expect ((int) result == -1, 0))
         __asm__ __volatile__("mov %0=ar.itc" : "=r"(result) :: "memory");
         return result;
      #elif defined(__sparc__)
         #if defined(__arch64__)
           ulong64 a;
           asm volatile("rd %%tick,%0" : "=r" (a));
           return a;
         #else
           register unsigned long x, y;
           __asm__ __volatile__ ("rd %%tick, %0; clruw %0, %1; srlx %0, 32, %0" : "=r" (x), "=r" (y) : "0" (x), "1" (y));
           return ((unsigned long long) x << 32) | y;
         #endif
      #else
         return XCLOCK();
      #endif

   /* Microsoft and Intel Windows compilers */
   #elif defined _M_IX86 && !defined(LTC_NO_ASM)
     __asm rdtsc
   #elif defined _M_AMD64 && !defined(LTC_NO_ASM)
     return __rdtsc ();
   #elif defined _M_IA64 && !defined(LTC_NO_ASM)
     #if defined __INTEL_COMPILER
       #include <ia64intrin.h>
     #endif
      return __getReg (3116);
   #else
     return XCLOCK();
   #endif
   }

static ulong64 timer, skew = 0;

static void t_start(void)
{
   timer = rdtsc();
}

static ulong64 t_read(void)
{
   return rdtsc() - timer;
}

static void init_timer(void)
{
   ulong64 c1, c2, t1, t2;
   unsigned long y1;

   c1 = c2 = (ulong64)-1;
   for (y1 = 0; y1 < TIMES*100; y1++) {
      t_start();
      t1 = t_read();
      t2 = (t_read() - t1)>>1;

      c1 = (t1 > c1) ? t1 : c1;
      c2 = (t2 > c2) ? t2 : c2;
   }
   skew = c2 - c1;
   fprintf(stderr, "Clock Skew: %lu\n", (unsigned long)skew);
}

static void time_keysched(void)
{
  unsigned long x, y1;
  ulong64 t1, c1;
  symmetric_key skey;
  int kl;
  int    (*func) (const unsigned char *, int , int , symmetric_key *);
  unsigned char key[MAXBLOCKSIZE];

  fprintf(stderr, "\n\nKey Schedule Time Trials for the Symmetric Ciphers:\n(Times are cycles per key)\n");
  no_results = 0;
 for (x = 0; cipher_descriptor[x].name != NULL; x++) {
#define DO1(k)   func(k, kl, 0, &skey);

    func = cipher_descriptor[x].setup;
    kl   = cipher_descriptor[x].min_key_length;
    c1 = (ulong64)-1;
    for (y1 = 0; y1 < KTIMES; y1++) {
       yarrow_read(key, kl, &yarrow_prng);
       t_start();
       DO1(key);
       t1 = t_read();
       c1 = (t1 > c1) ? c1 : t1;
    }
    t1 = c1 - skew;
    results[no_results].spd1 = results[no_results].avg = t1;
    results[no_results++].id = x;
    fprintf(stderr, "."); fflush(stdout);

#undef DO1
   }
   tally_results(0);
}

#ifdef LTC_ECB_MODE
static void time_cipher_ecb(void)
{
  unsigned long x, y1;
  ulong64  t1, t2, c1, c2, a1, a2;
  symmetric_ECB ecb;
  unsigned char key[MAXBLOCKSIZE] = { 0 }, pt[4096] = { 0 };
  int err;

  fprintf(stderr, "\n\nECB Time Trials for the Symmetric Ciphers:\n");
  no_results = 0;
  for (x = 0; cipher_descriptor[x].name != NULL; x++) {
    ecb_start(x, key, cipher_descriptor[x].min_key_length, 0, &ecb);

    /* sanity check on cipher */
    if ((err = cipher_descriptor[x].test()) != CRYPT_OK) {
       fprintf(stderr, "\n\nERROR: Cipher %s failed self-test %s\n", cipher_descriptor[x].name, error_to_string(err));
       exit(EXIT_FAILURE);
    }

#define DO1   ecb_encrypt(pt, pt, sizeof(pt), &ecb);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a1 = c2 - c1 - skew;

#undef DO1
#undef DO2
#define DO1   ecb_decrypt(pt, pt, sizeof(pt), &ecb);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a2 = c2 - c1 - skew;
    ecb_done(&ecb);

    results[no_results].id = x;
    results[no_results].spd1 = a1/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].spd2 = a2/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].avg = (results[no_results].spd1 + results[no_results].spd2+1)/2;
    ++no_results;
    fprintf(stderr, "."); fflush(stdout);

#undef DO2
#undef DO1
   }
   tally_results(1);
}
#else
static void time_cipher_ecb(void) { fprintf(stderr, "NO ECB\n"); return 0; }
#endif

#ifdef LTC_CBC_MODE
static void time_cipher_cbc(void)
{
  unsigned long x, y1;
  ulong64  t1, t2, c1, c2, a1, a2;
  symmetric_CBC cbc;
  unsigned char key[MAXBLOCKSIZE] = { 0 }, pt[4096] = { 0 };
  int err;

  fprintf(stderr, "\n\nCBC Time Trials for the Symmetric Ciphers:\n");
  no_results = 0;
  for (x = 0; cipher_descriptor[x].name != NULL; x++) {
    cbc_start(x, pt, key, cipher_descriptor[x].min_key_length, 0, &cbc);

    /* sanity check on cipher */
    if ((err = cipher_descriptor[x].test()) != CRYPT_OK) {
       fprintf(stderr, "\n\nERROR: Cipher %s failed self-test %s\n", cipher_descriptor[x].name, error_to_string(err));
       exit(EXIT_FAILURE);
    }

#define DO1   cbc_encrypt(pt, pt, sizeof(pt), &cbc);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a1 = c2 - c1 - skew;

#undef DO1
#undef DO2
#define DO1   cbc_decrypt(pt, pt, sizeof(pt), &cbc);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a2 = c2 - c1 - skew;
    cbc_done(&cbc);

    results[no_results].id = x;
    results[no_results].spd1 = a1/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].spd2 = a2/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].avg = (results[no_results].spd1 + results[no_results].spd2+1)/2;
    ++no_results;
    fprintf(stderr, "."); fflush(stdout);

#undef DO2
#undef DO1
   }
   tally_results(1);
}
#else
static void time_cipher_cbc(void) { fprintf(stderr, "NO CBC\n"); return 0; }
#endif

#ifdef LTC_CTR_MODE
static void time_cipher_ctr(void)
{
  unsigned long x, y1;
  ulong64  t1, t2, c1, c2, a1, a2;
  symmetric_CTR ctr;
  unsigned char key[MAXBLOCKSIZE] = { 0 }, pt[4096] = { 0 };
  int err;

  fprintf(stderr, "\n\nCTR Time Trials for the Symmetric Ciphers:\n");
  no_results = 0;
  for (x = 0; cipher_descriptor[x].name != NULL; x++) {
    ctr_start(x, pt, key, cipher_descriptor[x].min_key_length, 0, CTR_COUNTER_LITTLE_ENDIAN, &ctr);

    /* sanity check on cipher */
    if ((err = cipher_descriptor[x].test()) != CRYPT_OK) {
       fprintf(stderr, "\n\nERROR: Cipher %s failed self-test %s\n", cipher_descriptor[x].name, error_to_string(err));
       exit(EXIT_FAILURE);
    }

#define DO1   ctr_encrypt(pt, pt, sizeof(pt), &ctr);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a1 = c2 - c1 - skew;

#undef DO1
#undef DO2
#define DO1   ctr_decrypt(pt, pt, sizeof(pt), &ctr);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a2 = c2 - c1 - skew;
    ctr_done(&ctr);

    results[no_results].id = x;
    results[no_results].spd1 = a1/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].spd2 = a2/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].avg = (results[no_results].spd1 + results[no_results].spd2+1)/2;
    ++no_results;
    fprintf(stderr, "."); fflush(stdout);

#undef DO2
#undef DO1
   }
   tally_results(1);
}
#else
static void time_cipher_ctr(void) { fprintf(stderr, "NO CTR\n"); return 0; }
#endif

#ifdef LTC_LRW_MODE
static void time_cipher_lrw(void)
{
  unsigned long x, y1;
  ulong64  t1, t2, c1, c2, a1, a2;
  symmetric_LRW lrw;
  unsigned char key[MAXBLOCKSIZE] = { 0 }, pt[4096] = { 0 };
  int err;

  fprintf(stderr, "\n\nLRW Time Trials for the Symmetric Ciphers:\n");
  no_results = 0;
  for (x = 0; cipher_descriptor[x].name != NULL; x++) {
    if (cipher_descriptor[x].block_length != 16) continue;
    lrw_start(x, pt, key, cipher_descriptor[x].min_key_length, key, 0, &lrw);

    /* sanity check on cipher */
    if ((err = cipher_descriptor[x].test()) != CRYPT_OK) {
       fprintf(stderr, "\n\nERROR: Cipher %s failed self-test %s\n", cipher_descriptor[x].name, error_to_string(err));
       exit(EXIT_FAILURE);
    }

#define DO1   lrw_encrypt(pt, pt, sizeof(pt), &lrw);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a1 = c2 - c1 - skew;

#undef DO1
#undef DO2
#define DO1   lrw_decrypt(pt, pt, sizeof(pt), &lrw);
#define DO2   DO1 DO1

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < 100; y1++) {
        t_start();
        DO1;
        t1 = t_read();
        DO2;
        t2 = t_read();
        t2 -= t1;

        c1 = (t1 > c1 ? c1 : t1);
        c2 = (t2 > c2 ? c2 : t2);
    }
    a2 = c2 - c1 - skew;

    lrw_done(&lrw);

    results[no_results].id = x;
    results[no_results].spd1 = a1/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].spd2 = a2/(sizeof(pt)/cipher_descriptor[x].block_length);
    results[no_results].avg = (results[no_results].spd1 + results[no_results].spd2+1)/2;
    ++no_results;
    fprintf(stderr, "."); fflush(stdout);

#undef DO2
#undef DO1
   }
   tally_results(1);
}
#else
static void time_cipher_lrw(void) { fprintf(stderr, "NO LRW\n"); }
#endif


static void time_hash(void)
{
  unsigned long x, y1, len;
  ulong64 t1, t2, c1, c2;
  hash_state md;
  int    (*func)(hash_state *, const unsigned char *, unsigned long), err;
  unsigned char pt[MAXBLOCKSIZE] = { 0 };


  fprintf(stderr, "\n\nHASH Time Trials for:\n");
  no_results = 0;
  for (x = 0; hash_descriptor[x].name != NULL; x++) {

    /* sanity check on hash */
    if ((err = hash_descriptor[x].test()) != CRYPT_OK) {
       fprintf(stderr, "\n\nERROR: Hash %s failed self-test %s\n", hash_descriptor[x].name, error_to_string(err));
       exit(EXIT_FAILURE);
    }

    hash_descriptor[x].init(&md);

#define DO1   func(&md,pt,len);
#define DO2   DO1 DO1

    func = hash_descriptor[x].process;
    len  = hash_descriptor[x].blocksize;

    c1 = c2 = (ulong64)-1;
    for (y1 = 0; y1 < TIMES; y1++) {
       t_start();
       DO1;
       t1 = t_read();
       DO2;
       t2 = t_read() - t1;
       c1 = (t1 > c1) ? c1 : t1;
       c2 = (t2 > c2) ? c2 : t2;
    }
    t1 = c2 - c1 - skew;
    t1 = ((t1 * CONST64(1000))) / ((ulong64)hash_descriptor[x].blocksize);
    results[no_results].id = x;
    results[no_results].spd1 = results[no_results].avg = t1;
    ++no_results;
    fprintf(stderr, "."); fflush(stdout);
#undef DO2
#undef DO1
   }
   tally_results(2);
}

/*#warning you need an mp_rand!!!*/
#if !defined(USE_LTM) && !defined(USE_TFM) && !defined(USE_GMP) && !defined(EXT_MATH_LIB)
  #undef LTC_MPI
  #undef LTC_TEST_MPI
#else
  #define LTC_TEST_MPI
#endif

#ifdef LTC_MPI
static void time_mult(void)
{
   ulong64 t1, t2;
   unsigned long x, y;
   void  *a, *b, *c;

   fprintf(stderr, "Timing Multiplying:\n");
   mp_init_multi(&a,&b,&c,NULL);
   for (x = 128/MP_DIGIT_BIT; x <= (unsigned long)1536/MP_DIGIT_BIT; x += 128/MP_DIGIT_BIT) {
       mp_rand(a, x);
       mp_rand(b, x);

#define DO1 mp_mul(a, b, c);
#define DO2 DO1; DO1;

       t2 = -1;
       for (y = 0; y < TIMES; y++) {
           t_start();
           t1 = t_read();
           DO2;
           t1 = (t_read() - t1)>>1;
           if (t1 < t2) t2 = t1;
       }
       fprintf(stderr, "%4lu bits: %9"PRI64"u cycles\n", x*MP_DIGIT_BIT, t2);
   }
   mp_clear_multi(a,b,c,NULL);

#undef DO1
#undef DO2
}

static void time_sqr(void)
{
   ulong64 t1, t2;
   unsigned long x, y;
   void *a, *b;

   fprintf(stderr, "Timing Squaring:\n");
   mp_init_multi(&a,&b,NULL);
   for (x = 128/MP_DIGIT_BIT; x <= (unsigned long)1536/MP_DIGIT_BIT; x += 128/MP_DIGIT_BIT) {
       mp_rand(a, x);

#define DO1 mp_sqr(a, b);
#define DO2 DO1; DO1;

       t2 = -1;
       for (y = 0; y < TIMES; y++) {
           t_start();
           t1 = t_read();
           DO2;
           t1 = (t_read() - t1)>>1;
           if (t1 < t2) t2 = t1;
       }
       fprintf(stderr, "%4lu bits: %9"PRI64"u cycles\n", x*MP_DIGIT_BIT, t2);
   }
   mp_clear_multi(a,b,NULL);

#undef DO1
#undef DO2
}
#else
static void time_mult(void) { fprintf(stderr, "NO MULT\n"); }
static void time_sqr(void) { fprintf(stderr, "NO SQR\n"); }
#endif

static void time_prng(void)
{
   ulong64 t1, t2;
   unsigned char buf[4096];
   prng_state tprng;
   unsigned long x, y;
   int           err;

   fprintf(stderr, "Timing PRNGs (cycles/byte output, cycles add_entropy (32 bytes) :\n");
   for (x = 0; prng_descriptor[x].name != NULL; x++) {

      /* sanity check on prng */
      if ((err = prng_descriptor[x].test()) != CRYPT_OK) {
         fprintf(stderr, "\n\nERROR: PRNG %s failed self-test %s\n", prng_descriptor[x].name, error_to_string(err));
         exit(EXIT_FAILURE);
      }

      prng_descriptor[x].start(&tprng);
      zeromem(buf, 256);
      prng_descriptor[x].add_entropy(buf, 256, &tprng);
      prng_descriptor[x].ready(&tprng);
      t2 = -1;

#define DO1 if (prng_descriptor[x].read(buf, 4096, &tprng) != 4096) { fprintf(stderr, "\n\nERROR READ != 4096\n\n"); exit(EXIT_FAILURE); }
#define DO2 DO1 DO1
      for (y = 0; y < 10000; y++) {
         t_start();
         t1 = t_read();
         DO2;
         t1 = (t_read() - t1)>>1;
         if (t1 < t2) t2 = t1;
      }
      fprintf(stderr, "%20s: %5"PRI64"u ", prng_descriptor[x].name, t2>>12);
#undef DO2
#undef DO1

#define DO1 prng_descriptor[x].start(&tprng); prng_descriptor[x].add_entropy(buf, 32, &tprng); prng_descriptor[x].ready(&tprng); prng_descriptor[x].done(&tprng);
#define DO2 DO1 DO1
      for (y = 0; y < 10000; y++) {
         t_start();
         t1 = t_read();
         DO2;
         t1 = (t_read() - t1)>>1;
         if (t1 < t2) t2 = t1;
      }
      fprintf(stderr, "%5"PRI64"u\n", t2);
#undef DO2
#undef DO1

   }
}

#if defined(LTC_MDSA) && defined(LTC_TEST_MPI)
/* time various DSA operations */
static void time_dsa(void)
{
   dsa_key       key;
   ulong64       t1, t2;
   unsigned long x, y;
   int           err;
static const struct {
   int group, modulus;
} groups[] = {
{ 20, 96  },
{ 20, 128 },
{ 24, 192 },
{ 28, 256 },
#ifndef TFM_DESC
{ 32, 512 },
#endif
};

   for (x = 0; x < (sizeof(groups)/sizeof(groups[0])); x++) {
       t2 = 0;
       for (y = 0; y < 4; y++) {
           t_start();
           t1 = t_read();
           if ((err = dsa_generate_pqg(&yarrow_prng, find_prng("yarrow"), groups[x].group, groups[x].modulus, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\ndsa_generate_pqg says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           if ((err = dsa_generate_key(&yarrow_prng, find_prng("yarrow"), &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\ndsa_make_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;

#ifdef LTC_PROFILE
       t2 <<= 2;
       break;
#endif
           if (y < 3) {
              dsa_free(&key);
           }
       }
       t2 >>= 2;
       fprintf(stderr, "DSA-(%lu, %lu) make_key    took %15"PRI64"u cycles\n", (unsigned long)groups[x].group*8, (unsigned long)groups[x].modulus*8, t2);
       dsa_free(&key);
   }
   fprintf(stderr, "\n\n");
}
#else
static void time_dsa(void) { fprintf(stderr, "NO DSA\n"); }
#endif

#define LTC_TEST_MPI

static void printf_hex(unsigned char* hash_buffer, unsigned long w)
{
   unsigned long x;
   for (x = 0; x < w; x++) {
       printf("0x%02x ",hash_buffer[x]);
       if ((x+1) % 16 == 0) {
          printf("\n");
       }
   }
   printf("\n");
}

#if defined(LTC_MRSA) && defined(LTC_TEST_MPI)
/* time various RSA operations */
static void time_rsa(void)
{
   rsa_key       key;
   ulong64       t1, t2;
   unsigned char buf[2][2048] = { 0 };
   unsigned long x, y, z, zzz;
   int           err, zz, stat;

   for (x = 2048; x <= 2048; x += 256) {
//        t2 = 0;
//        for (y = 0; y < 4; y++) {
//            t_start();
//            t1 = t_read();
//            if ((err = rsa_make_key(&yarrow_prng, find_prng("yarrow"), x/8, 65537, &key)) != CRYPT_OK) {
//               fprintf(stderr, "\n\nrsa_make_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
//               exit(EXIT_FAILURE);
//            }
//            t1 = t_read() - t1;
//            t2 += t1;

// #ifdef LTC_PROFILE
//        t2 <<= 2;
//        break;
// #endif

//            if (y < 3) {
//               rsa_free(&key);
//            }
//        }
//        t2 >>= 2;
//        fprintf(stderr, "RSA-%lu make_key    took %15"PRI64"u cycles\n", x, t2);

//        t2 = 0;
//        for (y = 0; y < 16; y++) {
//            t_start();
//            t1 = t_read();
//            z = sizeof(buf[1]);
//            if ((err = rsa_encrypt_key(buf[0], 32, buf[1], &z, (const unsigned char *)"testprog", 8, &yarrow_prng,
//                                       find_prng("yarrow"), find_hash("sha1"),
//                                       &key)) != CRYPT_OK) {
//               fprintf(stderr, "\n\nrsa_encrypt_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
//               exit(EXIT_FAILURE);
//            }
//            t1 = t_read() - t1;
//            t2 += t1;
// #ifdef LTC_PROFILE
//        t2 <<= 4;
//        break;
// #endif
//        }
//        t2 >>= 4;
//        fprintf(stderr, "RSA-%lu encrypt_key took %15"PRI64"u cycles\n", x, t2);

//        t2 = 0;
//        for (y = 0; y < 2048; y++) {
//            t_start();
//            t1 = t_read();
//            zzz = sizeof(buf[0]);
//            if ((err = rsa_decrypt_key(buf[1], z, buf[0], &zzz, (const unsigned char *)"testprog", 8,  find_hash("sha1"),
//                                       &zz, &key)) != CRYPT_OK) {
//               fprintf(stderr, "\n\nrsa_decrypt_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
//               exit(EXIT_FAILURE);
//            }
//            t1 = t_read() - t1;
//            t2 += t1;
// #ifdef LTC_PROFILE
//        t2 <<= 11;
//        break;
// #endif
//        }
//        t2 >>= 11;
//        fprintf(stderr, "RSA-%lu decrypt_key took %15"PRI64"u cycles\n", x, t2);

#include "../notes/rsa-testvectors/pkcs1v15crypt-vectors.c"
      for (int i = 0; i < sizeof(testcases_eme)/sizeof(testcases_eme[0]); ++i) {
         testcase_t* t = &testcases_eme[i];
         if (t->rsa.n_l == x/8) {
            printf("t->name:%s\n", t->name);
            // rsa_set_key(t->rsa.n, t->rsa.n_l, t->rsa.e, t->rsa.e_l, t->rsa.d, t->rsa.d_l, &key);
            mp_init_multi(&key.e, &key.d, &key.N, &key.dQ, &key.dP, &key.qP, &key.p, &key.q, NULL);
            mp_read_unsigned_bin(key.e, t->rsa.e, t->rsa.e_l);
            mp_read_unsigned_bin(key.d, t->rsa.d, t->rsa.d_l);
            mp_read_unsigned_bin(key.N, t->rsa.n, t->rsa.n_l);
#ifdef CRT_SUPPORT
            printf("crt support!\n");
            mp_read_unsigned_bin(key.dQ, t->rsa.dQ, t->rsa.dQ_l);
            mp_read_unsigned_bin(key.dP, t->rsa.dP, t->rsa.dP_l);
            mp_read_unsigned_bin(key.qP, t->rsa.qInv, t->rsa.qInv_l);
            mp_read_unsigned_bin(key.q, t->rsa.q, t->rsa.q_l);
            mp_read_unsigned_bin(key.p, t->rsa.p, t->rsa.p_l);
#else
            printf("no crt!\n");
#endif
            key.type = PK_PRIVATE;
            break;
         }
      }

#define SIGN_TIMES 1000
      struct timeval start, end;
      gettimeofday(&start, NULL);
       t2 = 0;
       for (y = 0; y < SIGN_TIMES; y++) {
          t_start();
          t1 = t_read();
          z = sizeof(buf[1]);
         //  if ((err = rsa_sign_hash(buf[0], 20, buf[1], &z, &yarrow_prng,
         //                           find_prng("yarrow"), find_hash("sha1"), 8, &key)) != CRYPT_OK) {
          if ((err = rsa_sign_hash_ex(buf[0], 32, buf[1], &z, LTC_PKCS_1_V1_5, &yarrow_prng,
                                   find_prng("yarrow"), find_hash("sha256"), 32, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\nrsa_sign_hash says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;
#ifdef LTC_PROFILE
       t2 <<= 8;
       break;
#endif
        }
        gettimeofday(&end, NULL);
         // printf("buf[0]:\n");
         // printf_hex(buf[0], 32);
         // printf("buf[1](%d):\n", z);
         // printf_hex(buf[1], z);
      int cost = (end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec;
      fprintf(stderr, "\nKey length %lu, sign %d times cost %d us, avg %d us\n",
                    x, SIGN_TIMES, cost, cost / SIGN_TIMES);
        t2 >>= 8;
        fprintf(stderr, "RSA-%lu sign_hash took   %15"PRI64"u cycles\n", x, t2);

       t2 = 0;
       for (y = 0; y < SIGN_TIMES; y++) {
          t_start();
          t1 = t_read();
         //  if ((err = rsa_verify_hash(buf[1], z, buf[0], 20, find_hash("sha1"), 8, &stat, &key)) != CRYPT_OK) {
          if ((err = rsa_verify_hash_ex(buf[1], z, buf[0], 32, LTC_PKCS_1_V1_5, find_hash("sha256"), 32, &stat, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\nrsa_verify_hash says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
          }
          if (stat == 0) {
             fprintf(stderr, "\n\nrsa_verify_hash for RSA-%lu failed to verify signature(%lu)\n", x, y);
             exit(EXIT_FAILURE);
          }
          t1 = t_read() - t1;
          t2 += t1;
#ifdef LTC_PROFILE
       t2 <<= 11;
       break;
#endif
        }
        t2 >>= 11;
        fprintf(stderr, "RSA-%lu verify_hash took %15"PRI64"u cycles\n", x, t2);
       fprintf(stderr, "\n\n");
       rsa_free(&key);
  }
}
#else
static void time_rsa(void) { fprintf(stderr, "NO RSA\n"); }
#endif

#if defined(LTC_MKAT) && defined(LTC_TEST_MPI)
/* time various KAT operations */
static void time_katja(void)
{
   katja_key key;
   ulong64 t1, t2;
   unsigned char buf[2][4096];
   unsigned long x, y, z, zzz;
   int           err, zz;

   for (x = 1024; x <= 2048; x += 256) {
       t2 = 0;
       for (y = 0; y < 4; y++) {
           t_start();
           t1 = t_read();
           if ((err = katja_make_key(&yarrow_prng, find_prng("yarrow"), x/8, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\nkatja_make_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;

           if (y < 3) {
              katja_free(&key);
           }
       }
       t2 >>= 2;
       fprintf(stderr, "Katja-%lu make_key    took %15"PRI64"u cycles\n", x, t2);

       t2 = 0;
       for (y = 0; y < 16; y++) {
           t_start();
           t1 = t_read();
           z = sizeof(buf[1]);
           if ((err = katja_encrypt_key(buf[0], 32, buf[1], &z, "testprog", 8, &yarrow_prng,
                                      find_prng("yarrow"), find_hash("sha1"),
                                      &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\nkatja_encrypt_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;
       }
       t2 >>= 4;
       fprintf(stderr, "Katja-%lu encrypt_key took %15"PRI64"u cycles\n", x, t2);

       t2 = 0;
       for (y = 0; y < 2048; y++) {
           t_start();
           t1 = t_read();
           zzz = sizeof(buf[0]);
           if ((err = katja_decrypt_key(buf[1], z, buf[0], &zzz, "testprog", 8,  find_hash("sha1"),
                                      &zz, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\nkatja_decrypt_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;
       }
       t2 >>= 11;
       fprintf(stderr, "Katja-%lu decrypt_key took %15"PRI64"u cycles\n", x, t2);


       katja_free(&key);
  }
}
#else
static void time_katja(void) { fprintf(stderr, "NO Katja\n"); }
#endif

#if defined(LTC_MDH) && defined(LTC_TEST_MPI)
/* time various DH operations */
static void time_dh(void)
{
   dh_key key;
   ulong64 t1, t2;
   unsigned long i, x, y;
   int           err;
   static unsigned long sizes[] = {768/8, 1024/8, 1536/8, 2048/8,
#ifndef TFM_DESC
                                   3072/8, 4096/8, 6144/8, 8192/8,
#endif
                                   100000
   };

   for (x = sizes[i=0]; x < 100000; x = sizes[++i]) {
       t2 = 0;
       for (y = 0; y < 16; y++) {
           if((err = dh_set_pg_groupsize(x, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\ndh_set_pg_groupsize says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }

           t_start();
           t1 = t_read();
           if ((err = dh_generate_key(&yarrow_prng, find_prng("yarrow"), &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\ndh_make_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;

           dh_free(&key);
       }
       t2 >>= 4;
       fprintf(stderr, "DH-%4lu make_key    took %15"PRI64"u cycles\n", x*8, t2);
  }
}
#else
static void time_dh(void) { fprintf(stderr, "NO DH\n"); }
#endif

#if defined(LTC_MECC) && defined(LTC_TEST_MPI)
/* time various ECC operations */
static void time_ecc(void)
{
   ecc_key key;
   ulong64 t1, t2;
   unsigned char buf[2][256] = { 0 };
   unsigned long i, w, x, y, z;
   int           err, stat;
   static unsigned long sizes[] = {
#ifdef LTC_ECC112
112/8,
#endif
#ifdef LTC_ECC128
128/8,
#endif
#ifdef LTC_ECC160
160/8,
#endif
#ifdef LTC_ECC192
192/8,
#endif
#ifdef LTC_ECC224
224/8,
#endif
#ifdef LTC_ECC256
256/8,
#endif
#ifdef LTC_ECC384
384/8,
#endif
#ifdef LTC_ECC521
521/8,
#endif
100000};

   for (x = sizes[i=0]; x < 100000; x = sizes[++i]) {
       t2 = 0;
       for (y = 0; y < 256; y++) {
           t_start();
           t1 = t_read();
           if ((err = ecc_make_key(&yarrow_prng, find_prng("yarrow"), x, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\necc_make_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;

#ifdef LTC_PROFILE
       t2 <<= 8;
       break;
#endif

           if (y < 255) {
              ecc_free(&key);
           }
       }
       t2 >>= 8;
       fprintf(stderr, "ECC-%lu make_key    took %15"PRI64"u cycles\n", x*8, t2);

       t2 = 0;
       for (y = 0; y < 256; y++) {
           t_start();
           t1 = t_read();
           z = sizeof(buf[1]);
           if ((err = ecc_encrypt_key(buf[0], 20, buf[1], &z, &yarrow_prng, find_prng("yarrow"), find_hash("sha1"),
                                      &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\necc_encrypt_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;
#ifdef LTC_PROFILE
       t2 <<= 8;
       break;
#endif
       }
       t2 >>= 8;
       fprintf(stderr, "ECC-%lu encrypt_key took %15"PRI64"u cycles\n", x*8, t2);

       t2 = 0;
       for (y = 0; y < 256; y++) {
           t_start();
           t1 = t_read();
           w = 20;
           if ((err = ecc_decrypt_key(buf[1], z, buf[0], &w, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\necc_decrypt_key says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;
#ifdef LTC_PROFILE
       t2 <<= 8;
       break;
#endif
       }
       t2 >>= 8;
       fprintf(stderr, "ECC-%lu decrypt_key took %15"PRI64"u cycles\n", x*8, t2);

       t2 = 0;
       for (y = 0; y < 256; y++) {
          t_start();
          t1 = t_read();
          z = sizeof(buf[1]);
          if ((err = ecc_sign_hash(buf[0], 20, buf[1], &z, &yarrow_prng,
                                   find_prng("yarrow"), &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\necc_sign_hash says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
           }
           t1 = t_read() - t1;
           t2 += t1;
#ifdef LTC_PROFILE
       t2 <<= 8;
       break;
#endif
        }
        t2 >>= 8;
        fprintf(stderr, "ECC-%lu sign_hash took   %15"PRI64"u cycles\n", x*8, t2);

       t2 = 0;
       for (y = 0; y < 256; y++) {
          t_start();
          t1 = t_read();
          if ((err = ecc_verify_hash(buf[1], z, buf[0], 20, &stat, &key)) != CRYPT_OK) {
              fprintf(stderr, "\n\necc_verify_hash says %s, wait...no it should say %s...damn you!\n", error_to_string(err), error_to_string(CRYPT_OK));
              exit(EXIT_FAILURE);
          }
          if (stat == 0) {
             fprintf(stderr, "\n\necc_verify_hash for ECC-%lu failed to verify signature(%lu)\n", x*8, y);
             exit(EXIT_FAILURE);
          }
          t1 = t_read() - t1;
          t2 += t1;
#ifdef LTC_PROFILE
       t2 <<= 8;
       break;
#endif
        }
        t2 >>= 8;
        fprintf(stderr, "ECC-%lu verify_hash took %15"PRI64"u cycles\n", x*8, t2);

       fprintf(stderr, "\n\n");
       ecc_free(&key);
  }
}
#else
static void time_ecc(void) { fprintf(stderr, "NO ECC\n"); }
#endif

static void time_macs_(unsigned long MAC_SIZE)
{
#if defined(LTC_OMAC) || defined(LTC_XCBC) || defined(LTC_F9_MODE) || defined(LTC_PMAC) || defined(LTC_PELICAN) || defined(LTC_HMAC)
   unsigned char *buf, key[16], tag[16];
   ulong64 t1, t2;
   unsigned long x, z;
   int err, cipher_idx, hash_idx;

   fprintf(stderr, "\nMAC Timings (cycles/byte on %luKB blocks):\n", MAC_SIZE);

   buf = XMALLOC(MAC_SIZE*1024);
   if (buf == NULL) {
      fprintf(stderr, "\n\nout of heap yo\n\n");
      exit(EXIT_FAILURE);
   }

   cipher_idx = find_cipher("aes");
   hash_idx   = find_hash("sha1");

   if (cipher_idx == -1 || hash_idx == -1) {
      fprintf(stderr, "Warning the MAC tests requires AES and SHA1 to operate... so sorry\n");
      exit(EXIT_FAILURE);
   }

   yarrow_read(buf, MAC_SIZE*1024, &yarrow_prng);
   yarrow_read(key, 16, &yarrow_prng);

#ifdef LTC_OMAC
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = omac_memory(cipher_idx, key, 16, buf, MAC_SIZE*1024, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\n\nomac-%s error... %s\n", cipher_descriptor[cipher_idx].name, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "OMAC-%s\t\t%9"PRI64"u\n", cipher_descriptor[cipher_idx].name, t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_XCBC
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = xcbc_memory(cipher_idx, key, 16, buf, MAC_SIZE*1024, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\n\nxcbc-%s error... %s\n", cipher_descriptor[cipher_idx].name, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "XCBC-%s\t\t%9"PRI64"u\n", cipher_descriptor[cipher_idx].name, t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_F9_MODE
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = f9_memory(cipher_idx, key, 16, buf, MAC_SIZE*1024, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\n\nF9-%s error... %s\n", cipher_descriptor[cipher_idx].name, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "F9-%s\t\t\t%9"PRI64"u\n", cipher_descriptor[cipher_idx].name, t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_PMAC
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = pmac_memory(cipher_idx, key, 16, buf, MAC_SIZE*1024, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\n\npmac-%s error... %s\n", cipher_descriptor[cipher_idx].name, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "PMAC-%s\t\t%9"PRI64"u\n", cipher_descriptor[cipher_idx].name, t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_PELICAN
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = pelican_memory(key, 16, buf, MAC_SIZE*1024, tag)) != CRYPT_OK) {
           fprintf(stderr, "\n\npelican error... %s\n", error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "PELICAN \t\t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_HMAC
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = hmac_memory(hash_idx, key, 16, buf, MAC_SIZE*1024, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\n\nhmac-%s error... %s\n", hash_descriptor[hash_idx].name, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "HMAC-%s\t\t%9"PRI64"u\n", hash_descriptor[hash_idx].name, t2/(ulong64)(MAC_SIZE*1024));
#endif

   XFREE(buf);
#else
   LTC_UNUSED_PARAM(MAC_SIZE);
   fprintf(stderr, "NO MACs\n");
#endif
}

static void time_macs(void)
{
   time_macs_(1);
   time_macs_(4);
   time_macs_(32);
}

static void time_encmacs_(unsigned long MAC_SIZE)
{
#if defined(LTC_EAX_MODE) || defined(LTC_OCB_MODE) || defined(LTC_OCB3_MODE) || defined(LTC_CCM_MODE) || defined(LTC_GCM_MODE)
   unsigned char *buf, IV[16], key[16], tag[16];
   ulong64 t1, t2;
   unsigned long x, z;
   int err, cipher_idx;
   symmetric_key skey;

   fprintf(stderr, "\nENC+MAC Timings (zero byte AAD, 16 byte IV, cycles/byte on %luKB blocks):\n", MAC_SIZE);

   buf = XMALLOC(MAC_SIZE*1024);
   if (buf == NULL) {
      fprintf(stderr, "\n\nout of heap yo\n\n");
      exit(EXIT_FAILURE);
   }

   cipher_idx = find_cipher("aes");

   yarrow_read(buf, MAC_SIZE*1024, &yarrow_prng);
   yarrow_read(key, 16, &yarrow_prng);
   yarrow_read(IV, 16, &yarrow_prng);

#ifdef LTC_EAX_MODE
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = eax_encrypt_authenticate_memory(cipher_idx, key, 16, IV, 16, NULL, 0, buf, MAC_SIZE*1024, buf, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\nEAX error... %s\n", error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "EAX \t\t\t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_OCB_MODE
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = ocb_encrypt_authenticate_memory(cipher_idx, key, 16, IV, buf, MAC_SIZE*1024, buf, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\nOCB error... %s\n", error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "OCB \t\t\t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_OCB3_MODE
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = ocb3_encrypt_authenticate_memory(cipher_idx, key, 16, IV, 15, (unsigned char*)"", 0, buf, MAC_SIZE*1024, buf, tag, &z)) != CRYPT_OK) {
           fprintf(stderr, "\nOCB3 error... %s\n", error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "OCB3 \t\t\t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));
#endif

#ifdef LTC_CCM_MODE
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = ccm_memory(cipher_idx, key, 16, NULL, IV, 16, NULL, 0, buf, MAC_SIZE*1024, buf, tag, &z, CCM_ENCRYPT)) != CRYPT_OK) {
           fprintf(stderr, "\nCCM error... %s\n", error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "CCM (no-precomp) \t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));

   cipher_descriptor[cipher_idx].setup(key, 16, 0, &skey);
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = ccm_memory(cipher_idx, key, 16, &skey, IV, 16, NULL, 0, buf, MAC_SIZE*1024, buf, tag, &z, CCM_ENCRYPT)) != CRYPT_OK) {
           fprintf(stderr, "\nCCM error... %s\n", error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "CCM (precomp) \t\t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));
   cipher_descriptor[cipher_idx].done(&skey);
#endif

#ifdef LTC_GCM_MODE
   t2 = -1;
   for (x = 0; x < 100; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = gcm_memory(cipher_idx, key, 16, IV, 16, NULL, 0, buf, MAC_SIZE*1024, buf, tag, &z, GCM_ENCRYPT)) != CRYPT_OK) {
           fprintf(stderr, "\nGCM error... %s\n", error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "GCM (no-precomp)\t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));

   {
   gcm_state gcm
#ifdef LTC_GCM_TABLES_SSE2
__attribute__ ((aligned (16)))
#endif
;

   if ((err = gcm_init(&gcm, cipher_idx, key, 16)) != CRYPT_OK) { fprintf(stderr, "gcm_init: %s\n", error_to_string(err)); exit(EXIT_FAILURE); }
   t2 = -1;
   for (x = 0; x < 10000; x++) {
        t_start();
        t1 = t_read();
        z = 16;
        if ((err = gcm_reset(&gcm)) != CRYPT_OK) {
            fprintf(stderr, "\nGCM error[%d]... %s\n", __LINE__, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        if ((err = gcm_add_iv(&gcm, IV, 16)) != CRYPT_OK) {
            fprintf(stderr, "\nGCM error[%d]... %s\n", __LINE__, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        if ((err = gcm_add_aad(&gcm, NULL, 0)) != CRYPT_OK) {
            fprintf(stderr, "\nGCM error[%d]... %s\n", __LINE__, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        if ((err = gcm_process(&gcm, buf, MAC_SIZE*1024, buf, GCM_ENCRYPT)) != CRYPT_OK) {
            fprintf(stderr, "\nGCM error[%d]... %s\n", __LINE__, error_to_string(err));
           exit(EXIT_FAILURE);
        }

        if ((err = gcm_done(&gcm, tag, &z)) != CRYPT_OK) {
            fprintf(stderr, "\nGCM error[%d]... %s\n", __LINE__, error_to_string(err));
           exit(EXIT_FAILURE);
        }
        t1 = t_read() - t1;
        if (t1 < t2) t2 = t1;
   }
   fprintf(stderr, "GCM (precomp)\t\t%9"PRI64"u\n", t2/(ulong64)(MAC_SIZE*1024));
   }

#endif
   XFREE(buf);
#else
   LTC_UNUSED_PARAM(MAC_SIZE);
   fprintf(stderr, "NO ENCMACs\n");
#endif

}

static void time_encmacs(void)
{
   time_encmacs_(1);
   time_encmacs_(4);
   time_encmacs_(32);
}

#define LTC_TEST_FN(f)  { f, #f }
int main(int argc, char **argv)
{
int err;

const struct
{
   void (*fn)(void);
   const char* name;
} test_functions[] = {
   LTC_TEST_FN(time_keysched),
   LTC_TEST_FN(time_cipher_ecb),
   LTC_TEST_FN(time_cipher_cbc),
   LTC_TEST_FN(time_cipher_ctr),
   LTC_TEST_FN(time_cipher_lrw),
   LTC_TEST_FN(time_hash),
   LTC_TEST_FN(time_macs),
   LTC_TEST_FN(time_encmacs),
   LTC_TEST_FN(time_prng),
   LTC_TEST_FN(time_mult),
   LTC_TEST_FN(time_sqr),
   LTC_TEST_FN(time_rsa),
   LTC_TEST_FN(time_dsa),
   LTC_TEST_FN(time_ecc),
   LTC_TEST_FN(time_dh),
   LTC_TEST_FN(time_katja)
};
char *single_test = NULL;
unsigned int i;

init_timer();
register_all_ciphers();
register_all_hashes();
register_all_prngs();

#ifdef USE_LTM
   ltc_mp = ltm_desc;
#elif defined(USE_TFM)
   ltc_mp = tfm_desc;
#elif defined(USE_GMP)
   ltc_mp = gmp_desc;
#elif defined(EXT_MATH_LIB)
   {
      printf("EXT_MATH_LIB\n");
      extern ltc_math_descriptor EXT_MATH_LIB;
      ltc_mp = EXT_MATH_LIB;
   }
#endif

if ((err = rng_make_prng(128, find_prng("yarrow"), &yarrow_prng, NULL)) != CRYPT_OK) {
   fprintf(stderr, "rng_make_prng failed: %s\n", error_to_string(err));
   exit(EXIT_FAILURE);
}

/* single test name from commandline */
if (argc > 1) single_test = argv[1];

for (i = 0; i < sizeof(test_functions)/sizeof(test_functions[0]); ++i) {
   if (single_test && strstr(test_functions[i].name, single_test) == NULL) {
     continue;
   }
   test_functions[i].fn();
}

return EXIT_SUCCESS;

}

/* ref:         $Format:%D$ */
/* git commit:  $Format:%H$ */
/* commit time: $Format:%ai$ */
