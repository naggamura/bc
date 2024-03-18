/* bi.c: Implements arbitrary precision numbers in base 10^k */
/*
    Copyright (C) 2015 Free Software Foundation, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License , or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; see the file COPYING.  If not, write to:

      The Free Software Foundation, Inc.
      51 Franklin Street, Fifth Floor
      Boston, MA 02110-1301  USA


    You may contact the author by:
       e-mail:  lalala72@gmail.com

*************************************************************************/

#include	<stdio.h>
#include	<stdlib.h>
#include	<memory.h>

#define PARAL_MULT 1

#ifdef	PARAL_MULT	// Supports multi-threaded multiplication.
	#include	<pthread.h>
#endif

#include	<assert.h>
#include	<unistd.h>
#include	"bi.h"

//#define		BI_ASSERT(x)
#define		BI_ASSERT(x)	assert(x)

#define		KARATSUBA_CUTOFF	64
#define		TC3_CUTOFF			2048

#define		MAX_PARAL_CNT		125	/* 5^3 */
#define		PARAL_START_DEPTH	2
#define		MIN_PARAL_BLK_SIZE	512

#ifdef	PARAL_MULT
static int	__g_paral_cnt = 1;
#endif

static dig_t	g_pow10[LOG10_RADIX];

static len_t	__strlen(const char* str)
{
	len_t	len = 0;

	if(str)
	{
		while(*(str++))
		{
			len++;
		}
	}
	return len;
}

int		bi_from_string(dig_t* a, len_t* alen, const char* str, len_t len)
{
	len_t	i, j;
	len_t	qq = len / LOG10_RADIX;
	len_t	aalen = 0;
	dig_t	t, c;
	char	ch;

	if(len < 0)
	{
		len = __strlen(str);
	}

	for(i = len-1; qq > 0; qq--)
	{
		t = 0;
		for(j = 0; j < LOG10_RADIX; j++)
		{
			ch = str[i--];

			if(ch >= '0' && ch <= '9')
			{
				c = ch - '0';
			}
			else
			{
				return 0;
			}
			c *= g_pow10[j];
			t += c;
		}
		a[aalen++] = t;
	}

	if(i >= 0)
	{
		t = 0;
		for(j = 0; j < LOG10_RADIX && i >= 0; j++)
		{
			ch = str[i--];

			if(ch >= '0' && ch <= '9')
			{
				c = ch - '0';
			}
			else
			{
				return 0;
			}
			c *= g_pow10[j];
			t += c;
		}
		a[aalen++] = t;
	}

	while(aalen > 0 && 0 == a[aalen-1])
	{
		aalen--;
	}
	
	*alen = aalen;
	return 1;
}

int		bi_from_chars(dig_t* a, len_t* alen, const char* str, len_t len)
{
	len_t	i, j;
	len_t	qq = len / LOG10_RADIX;
	len_t	aalen = 0;
	dig_t	t;

	for(i = len-1; qq > 0; qq--)
	{
		t = 0;
		for(j = 0; j < LOG10_RADIX; j++)
		{
			t += g_pow10[j]*str[i--];
		}
		a[aalen++] = t;
	}

	if(i >= 0)
	{
		t = 0;
		for(j = 0; j < LOG10_RADIX && i >= 0; j++)
		{
			t += g_pow10[j]*str[i--];
		}
		a[aalen++] = t;
	}

	while(aalen > 0 && 0 == a[aalen-1])
	{
		aalen--;
	}
	
	*alen = aalen;
	return 1;
}

len_t	bi_to_chars(const dig_t* a, len_t alen, char* str)
{
	len_t	i, j, k;
	dig_t	d;
	int		tc[LOG10_RADIX];

	BI_ASSERT(alen == 0 || (alen > 0 && a[alen-1] > 0));

	if(alen <= 0)
	{
		return 0;
	}

	k = 0;

	{
		d = a[alen-1];
		for(j = LOG10_RADIX-1; j >= 0; j--)
		{
			tc[j] = (d%10);
			d /= 10;
		}
		for(j = 0; 0 == tc[j]; j++)
		{
			;
		}
		for(; j < LOG10_RADIX; j++)
		{
			*(str++) = (char)(tc[j]);
			k++;
		}
	}

	for(i = alen-2; i >= 0; i--)
	{
		d = a[i];
		str += LOG10_RADIX;
		for(j = 0; j < LOG10_RADIX; j++)
		{
			*(--str) = (d%10);
			d /= 10;
		}
		str += LOG10_RADIX;
		k += LOG10_RADIX;
	}

	return k;
}



#define		UNLIKELY(e)		__builtin_expect(e, 0)



#if		0
static len_t	__suff_digits(dig_t a, len_t b)
{
	if(a <= 1)
	{
		return 1;
	}
	if(a <= 10)
	{
		return ((len_t)25)*b/100 + 1;
	}
	if(a <= 100)
	{
		return ((len_t)38)*b/100 + 1;
	}
	if(a <= 10000)
	{
		return ((len_t)50)*b/100 + 1;
	}
	if(a <= 100000)
	{
		return ((len_t)63)*b/100 + 1;
	}
	if(a <= 1000000)
	{
		return ((len_t)75)*b/100 + 1;
	}
	if(a <= 10000000)
	{
		return ((len_t)88)*b/100 + 1;
	}
	return b + 1;	/* a <= 100000000 */
/*
	if(a <= 1000000000)
	{
		return ((len_t)113)*b/100 + 1;
	}
	if(a <= 0x7FFFFFFF)
	{
		return ((len_t)125)*b/100 + 1;
	}
	fprintf(stderr, "Too big exponent %d\n", b);
	exit(-1);
*/
}
#endif

int		bi_cmp(const dig_t* a, len_t alen, const dig_t* b, len_t blen)
{
	if(alen > blen)
	{
		return 1;
	}
	if(alen < blen)
	{
		return -1;
	}

	{
		len_t	i;

		for(i = alen-1; i >= 0; i--)
		{
			if(a[i] > b[i])
			{
				return 1;
			}
			if(a[i] < b[i])
			{
				return -1;
			}
		}
	}	
	return 0;
}

static int	__cmp_shift(const dig_t* a, len_t alen, const dig_t* b, len_t blen, len_t s)
{
	len_t	bblen = blen + s;
	
	if(alen > bblen)
	{
		return 1;
	}
	if(alen < bblen)
	{
		return -1;
	}

	{
		len_t	i;

		for(i = alen-1; i >= s; i--)
		{
			if(a[i] > b[i-s])
			{
				return 1;
			}
			if(a[i] < b[i-s])
			{
				return -1;
			}
		}
		for(; i >= 0 && 0 == a[i]; i--)
		{
			;
		}
		if(i >= 0)
		{
			return 1;
		}
	}	
	return 0;
}

#if 0
static void		bi_lshift(dig_t* a, len_t* alen, len_t d)
{
	if(UNLIKELY(*alen <= 0 || d <= 0))
	{
		return;
	}

	{
		len_t	i;

		for(i = *alen-1; i >= 0; i--)
		{
			a[i+d] = a[i];
		}
		for(i = d-1; i >= 0; i--)
		{
			a[i] = 0;
		}
		*alen += d;
	}
}
#endif

#if 0
static void		bi_rshift(dig_t* a, len_t* alen, len_t d)
{
	if(UNLIKELY(*alen <= 0 || d <= 0))
	{
		return;
	}
	if(UNLIKELY(*alen <= d))
	{
		*alen = 0;
		return;
	}

	{
		len_t	i, aalen = *alen;

		for(i = d; i < aalen; i++)
		{
			a[i-d] = a[i];
		}
		*alen -= d;
	}
}
#endif

void	bi_inc(dig_t* a, len_t* alen)
{
	len_t	i, aalen = *alen;

	for(i = 0; i < aalen; i++)
	{
		a[i]++;
		if(a[i] < RADIX)
		{
			break;
		}
		else
		{
			a[i] = 0;
		}
	}

	if(i < aalen)
	{
	}
	else
	{
		a[i++] = 1;
		(*alen)++;
	}
}

void	bi_dec(dig_t* a, len_t* alen)
{
	// a must be positive.
	len_t	i, aalen = *alen;

	BI_ASSERT(*alen > 0 && a[*alen-1] > 0);

	for(i = 0; i < aalen && a[i] == 0; i++)
	{
		a[i] = RADIX-1;
	}
	a[i]--;
	if(0 == a[aalen-1])
	{
		*alen = aalen-1;
	}
}

void	bi_add1(
	dig_t* a, len_t* alen,
	const dig_t* b, len_t blen)
{
	ddig_t	d = 0;
	len_t	i, aalen = *alen;

	if(UNLIKELY(blen <= 0))
	{
		return;
	}

	if(aalen <= blen)
	{	
		for(i = 0; i < aalen; i++)
		{
			d += a[i];
			d += b[i];
			if(d >= RADIX)
			{
				a[i] = (dig_t)(d - RADIX);
				d = 1;
			}
			else
			{
				a[i] = (dig_t)d;
				d = 0;
			}
		}

		for(; d && i < blen; i++)
		{
			d += b[i];
			if(d >= RADIX)
			{
				a[i] = (dig_t)(d - RADIX);
				d = 1;
			}
			else
			{
				a[i] = (dig_t)d;
				d = 0;
			}
		}

		for(; i < blen; i++)
		{
			a[i] = b[i];
		}
	}
	else
	{	
		for(i = 0; i < blen; i++)
		{
			d += a[i];
			d += b[i];
			if(d >= RADIX)
			{
				a[i] = (dig_t)(d - RADIX);
				d = 1;
			}
			else
			{
				a[i] = (dig_t)d;
				d = 0;
			}
		}

		for(; d && i < aalen; i++)
		{
			d += a[i];
			if(d >= RADIX)
			{
				a[i] = (dig_t)(d - RADIX);
				d = 1;
			}
			else
			{
				a[i] = (dig_t)d;
				d = 0;
			}
		}
		
		i = aalen;
	}

	if(d)
	{
		a[i++] = 1;
	}

	*alen = i;
}

void	bi_add2(
	const dig_t* a, len_t alen, 
	const dig_t* b, len_t blen, 
	dig_t* c, len_t* clen)
{
	ddig_t	d = 0;
	len_t	i;

	if(alen > blen)
	{
		const dig_t*	t;
		len_t	l;
		
		t = a;
		a = b;
		b = t;
		l = alen;
		alen = blen;
		blen = l;
	}

	for(i = 0; i < alen; i++)
	{
		d += a[i];
		d += b[i];
		if(d >= RADIX)
		{
			c[i] = (dig_t)(d - RADIX);
			d = 1;
		}
		else
		{
			c[i] = (dig_t)d;
			d = 0;
		}
	}

	for(; d && i < blen; i++)
	{
		d += b[i];
		if(d >= RADIX)
		{
			c[i] = (dig_t)(d - RADIX);
			d = 1;
		}
		else
		{
			c[i] = (dig_t)d;
			d = 0;
		}
	}

	for(; i < blen; i++)
	{
		c[i] = b[i];
	}

	if(d)
	{
		c[i++] = 1;
	}

	*clen = i;
}

void	bi_add_shift(
	dig_t* a, len_t* alen,
	const dig_t* b, len_t blen,
	len_t s)
{
	len_t	i, aalen = *alen;

	if(UNLIKELY(blen <= 0))
	{
		return;
	}

	if(aalen >= s)
	{
		aalen -= s;
	}
	else
	{
		for(i = aalen; i < s; i++)
		{
			a[i] = 0;
		}
		aalen = 0;
	}
	bi_add1(a+s, &aalen, b, blen);
	*alen = aalen + s;
}

void	bi_sub1(
	dig_t* a, len_t* alen,
	const dig_t* b, len_t blen)
{
	ddig_t	d = 0;
	len_t	i, aalen = *alen;

	if(UNLIKELY(blen <= 0))
	{
		return;
	}

	for(i = 0; i < blen; i++)
	{
		d += a[i];
		d -= b[i];
		if(d >= 0)
		{
			a[i] = (dig_t)d;
			d = 0;
		}
		else
		{
			a[i] = (dig_t)(d + RADIX);
			d = -1;
		}
	}

	for(; d && i < aalen; i++)
	{
		d += a[i];
		if(d >= 0)
		{
			a[i] = (dig_t)d;
			d = 0;
		}
		else
		{
			a[i] = (dig_t)(d + RADIX);
			d = -1;
		}
	}

	i = aalen-1;
	while(i >= 0 && 0 == a[i])
	{
		i--;
	}

	*alen = i+1;
}

void	bi_sub2(
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen,
	dig_t* c, len_t* clen)
{
	ddig_t	d = 0;
	len_t	i;

	for(i = 0; i < blen; i++)
	{
		d += a[i];
		d -= b[i];
		if(d >= 0)
		{
			c[i] = (dig_t)d;
			d = 0;
		}
		else
		{
			c[i] = (dig_t)(d + RADIX);
			d = -1;
		}
	}

	for(; d && i < alen; i++)
	{
		d += a[i];
		if(d >= 0)
		{
			c[i] = (dig_t)d;
			d = 0;
		}
		else
		{
			c[i] = (dig_t)(d + RADIX);
			d = -1;
		}
	}

	for(; i < alen; i++)
	{
		c[i] = a[i];
	}

	i = alen-1;
	while(i >= 0 && 0 == c[i])
	{
		i--;
	}

	*clen = i+1;
}

void	bi_sub_shift(
	dig_t* a, len_t* alen,
	const dig_t* b, len_t blen, len_t s)
{
	len_t	i, aalen = *alen;

	if(UNLIKELY(blen <= 0))
	{
		return;
	}

	aalen -= s;
	bi_sub1(a+s, &aalen, b, blen);
	aalen += s;

	i = aalen-1;
	while(i >= 0 && 0 == a[i])
	{
		i--;
	}

	*alen = i+1;
}

void	bi_elementary_mul(
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen,
	dig_t* c, len_t* pclen)
{
	ddig_t digdig, bdig;
	len_t i, j, k;

	if (!alen || !blen)
	{
		*pclen = 0;
		return;
	}

	if (alen < blen)
	{
		const dig_t *tp;
		len_t tl;

		tp = a;
		a = b;
		b = tp;

		tl = alen;
		alen = blen;
		blen = tl;
	}

	memset(c, 0, (size_t)(alen + blen) * sizeof(dig_t));
	k = 0;
	for (i = 0; i < blen; i++)
	{
		bdig = b[i];
		if (bdig)
		{
			digdig = 0;
			for (j = 0, k = i; j < alen; j++, k++)
			{
				digdig += ((ddig_t)c[k]) + ((ddig_t)a[j]) * bdig;
				c[k] = (dig_t)(digdig % RADIX);
				digdig /= RADIX;
			}
			if (digdig)
			{
				c[k++] = (dig_t)digdig;
			}
		}
	}
	*pclen = k;
}

static void		__mul_karatsuba(
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen,
	dig_t* c, len_t* clen)
{
	// #of allocated digits of c must be greater than or equal to (alen+blen+1)
	if(UNLIKELY(alen <= 0 || blen <= 0))
	{
		*clen = 0;
		return;
	}
	
	if(UNLIKELY(blen <= KARATSUBA_CUTOFF))
	{
		bi_elementary_mul(a, alen, b, blen, c, clen);
		return;
	}
/*
	if(2*blen <= alen)
	{
		dig_t*	t = bi_malloc(2*blen, __LINE__);
		len_t	ss = 0;

		*clen = 0;
		while(alen > 0)
		{
			len_t	l = (alen >= blen ? blen : alen);
			for(l--; l >= 0 && 0 == a[l]; l--)
			{
				;
			}
			l++;

			if(l > 0)
			{
				len_t	tlen;
				__mul_karatsuba(a, l, b, blen, t, &tlen);
				bi_add_shift(c, clen, t, tlen, ss);
			}
			a += blen;
			alen -= blen;
			ss += blen;
		}
		bi_free(t);
		return;
	}
*/
	{
		len_t	m;
		len_t	i;
		const dig_t	*x0, *x1, *y0, *y1;
		dig_t	*z0, *z1, *z2;
		len_t	x0len = 0, x1len = 0, y0len = 0, y1len = 0, z0len = 0, z1len = 0, z2len = 0, txlen, tylen;

		m = alen;
		if(m < blen)
		{
			m = blen;
		}

		m = (m+1)/2;

		if(alen > m)
		{
			x1 = a + m;
			x1len = alen - m;

			x0 = a;
			x0len = m;
			for(i = m-1; i >= 0 && 0 == x0[i]; i--)
			{
				;
			}
			x0len = i+1;
		}
		else
		{
			x1 = (dig_t*)0;
			x0 = a;
			x0len = alen;
		}

		if(blen > m)
		{
			y1 = b + m;
			y1len = blen - m;

			y0 = b;
			y0len = m;
			for(i = m-1; i >= 0 && 0 == y0[i]; i--)
			{
				;
			}
			y0len = i+1;
		}
		else
		{
			y1 = (dig_t*)0;
			y0 = b;
			y0len = blen;
		}

		// z0 = x0*y0;
		z0 = c;//bi_malloc(alen+blen, __LINE__);
		__mul_karatsuba(x0, x0len, y0, y0len, z0, &z0len);
		*clen = z0len;

		// z2 = x1*y1;
		z2 = bi_malloc(x1len+y1len, __LINE__);
		__mul_karatsuba(x1, x1len, y1, y1len, z2, &z2len);

		{
			dig_t	*tx, *ty;

			tx = bi_malloc(m+1, __LINE__);
			ty = bi_malloc(m+1, __LINE__);

			// z1 = (x1+x0)*(y1+y0)-z2-z0;
			memcpy(tx, x1, (size_t)x1len * sizeof(dig_t));
			txlen = x1len;
			bi_add1(tx, &txlen, x0, x0len);

			memcpy(ty, y1, (size_t)y1len * sizeof(dig_t));
			tylen = y1len;
			bi_add1(ty, &tylen, y0, y0len);

			z1 = bi_malloc(2*(m+1), __LINE__);
			__mul_karatsuba(tx, txlen, ty, tylen, z1, &z1len);
			bi_free(tx);
			bi_free(ty);
		}

		bi_sub1(z1, &z1len, z2, z2len);
		bi_sub1(z1, &z1len, z0, z0len);

		// c already contains z0

		// return x0*y0 + z1*r^m + z2*r^(2m)
		bi_add_shift(c, clen, z1, z1len, m);
		bi_free(z1);
		
		bi_add_shift(c, clen, z2, z2len, 2*m);
		bi_free(z2);
	}
}

static void		__sadd(int* asign, dig_t* a, len_t* alen, int bsign, const dig_t* b, len_t blen)
{
	int		aas = *asign;
	len_t	aalen = *alen;
	len_t	i;
	int		d;

	if((aas > 0 && bsign > 0) || (aas < 0 && bsign < 0))
	{
		bi_add1(a, alen, b, blen);
		return;
	}

	d = bi_cmp(a, aalen, b, blen);
	if(0 == d)
	{
		*asign = 1;
		*alen = 0;
		return;
	}

	if(d > 0)	// abs = (a-b), sign = as
	{
		bi_sub1(a, alen, b, blen);
	}
	else		// abs = (b-a), sign = -as
	{
		dig_t*	t = bi_malloc(aalen, __LINE__);
		len_t	tlen = aalen;
		for(i = 0; i < aalen; i++)
		{
			t[i] = a[i];
		}
		bi_sub2(b, blen, t, tlen, a, alen);
		*asign = -aas;
		bi_free(t);
	}
}

#if 0
static void		__mul1_one_digit(
	dig_t* a, len_t* alen,
	dig_t b)
{
	ddig_t	d = 0, bb = b;
	len_t	i, aalen = *alen;

	for(i = 0; i < aalen; i++)
	{
		d += a[i]*bb;
		a[i] = d%RADIX;
		d /= RADIX;
	}
	if(d)
	{
		a[i] = d;
		(*alen)++;
	}
}
#endif

static void		__mul2_one_digit(
	const dig_t* a, len_t alen,
	dig_t b,
	dig_t* c, len_t* clen)
{
	ddig_t	d = 0, bb = b;
	len_t	i;

	for(i = 0; i < alen; i++)
	{
		d += a[i]*bb;
		c[i] = (dig_t)(d%RADIX);
		d /= RADIX;
	}
	if(d)
	{
		c[i++] = (dig_t)d;
	}
	*clen = i;
}

static void		__div_by_2(
	dig_t*	a,
	len_t*	alen)
{
	ddig_t	d;
	len_t	i, j = 0, aalen = *alen;

	if(aalen > 0)
	{
		d = a[0];
		d *= RADIX/2;
		d /= RADIX;
		for(i = 1; i < aalen; i++)
		{
			d += a[i]*((ddig_t)(RADIX/2));
			a[j++] = d%RADIX;
			d /= RADIX;
		}
		if(d)
		{
			a[j++] = (dig_t)d;
		}
	}
	*alen = j;
}

// a must be a multiple of 3 and gcd(RADIX, 3) must be 1.
static void		__div_by_3(
	dig_t*	a,
	len_t*	alen)
{
	dig_t	*t;
	len_t	tlen, aalen = *alen;
	ddig_t	d;
	len_t	i;

	t = a;

	tlen = aalen;

	for(i = 0; i < aalen && tlen > 0; i++)
	{
		d = (((ddig_t)ARI_INV_3)*(*t))%RADIX;
		*(t++) = (dig_t)d;
		d *= 3;
		tlen--;
		if(d >= RADIX)
		{
			d /= RADIX;
			bi_sub1(t, &tlen, (dig_t*)&d, 1);
		}
	}
	*alen = (len_t)(t-a);
}

static void		__split3(
	const dig_t* a, len_t alen,
	len_t	bs,
	const dig_t** b0, len_t* b0len,
	const dig_t** b1, len_t* b1len,
	const dig_t** b2, len_t* b2len)
{
	const dig_t	*bb0, *bb1, *bb2;
	len_t	bb0len, bb1len, bb2len;

	bb0 = a;

	if(alen <= bs)
	{
		bb0len = alen;
		bb1 = (dig_t*)0;
		bb1len = 0;
		bb2 = (dig_t*)0;
		bb2len = 0;
	}
	else if(alen <= 2*bs)
	{
		bb0len = bs;
		bb1 = a + bs;
		bb1len = alen - bs;
		bb2 = (dig_t*)0;
		bb2len = 0;
	}
	else
	{
		bb0len = bs;
		bb1 = a + bs;
		bb1len = bs;
		bb2 = a + 2*bs;
		bb2len = alen - 2*bs;
	}

	for(bb0len--; bb0len >= 0 && 0 == bb0[bb0len]; bb0len--)
	{
		;
	}
	bb0len++;

	for(bb1len--; bb1len >= 0 && 0 == bb1[bb1len]; bb1len--)
	{
		;
	}
	bb1len++;

	for(bb2len--; bb2len >= 0 && 0 == bb2[bb2len]; bb2len--)
	{
		;
	}
	bb2len++;

	*b0 = bb0;
	*b0len = bb0len;
	*b1 = bb1;
	*b1len = bb1len;
	*b2 = bb2;
	*b2len = bb2len;
}

static void		__eval_tc3(
	const dig_t* c0, len_t c0len,
	const dig_t* c1, len_t c1len,
	const dig_t* c2, len_t c2len,
	int v,	// -2, -1 or 1
	int* rsign, dig_t* r, len_t* rlen)
{
	len_t	i;
	int		rrsign;
	len_t	rrlen;

	rrsign = 1;
	for(i = 0; i < c0len; i++)
	{
		r[i] = c0[i];
	}
	rrlen = c0len;

	if(1 == v)
	{
		bi_add1(r, &rrlen, c1, c1len);
		bi_add1(r, &rrlen, c2, c2len);
	}
	else if(-1 == v)
	{
		__sadd(&rrsign, r, &rrlen, -1, c1, c1len);
		__sadd(&rrsign, r, &rrlen, 1, c2, c2len);
	}
	else	// if(-2)
	{
		len_t	m = c0len, tlen;
		dig_t*	t;

		if(c1len > m)
		{
			m = c1len;
		}
		if(c2len > m)
		{
			m = c2len;
		}

		t = bi_malloc(m+1, __LINE__);

		__mul2_one_digit(c1, c1len, 2, t, &tlen);
		__sadd(&rrsign, r, &rrlen, -1, t, tlen);

		__mul2_one_digit(c2, c2len, 4, t, &tlen);
		__sadd(&rrsign, r, &rrlen, 1, t, tlen);

		bi_free(t);
	}

	*rsign = rrsign;
	*rlen = rrlen;
}

static void		__mul_toomcook3(
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen,
	dig_t* c, len_t* clen)
{
	// # of allocated digits of c must be greater than or equal to (alen+blen+1)
	if(UNLIKELY(alen <= 0 || blen <= 0))
	{
		*clen = 0;
		return;
	}

	if(UNLIKELY(blen < TC3_CUTOFF))
	{
		__mul_karatsuba(a, alen, b, blen, c, clen);
		return;
	}

	{
		len_t	m;
		//len_t	i;
		const dig_t	*x0, *x1, *x2;
		len_t	x0len = 0, x1len = 0, x2len = 0;
		const dig_t	*y0, *y1, *y2;
		len_t	y0len = 0, y1len = 0, y2len = 0;
		dig_t	*z0, *z1, *z2, *z3, *z4;
		len_t	z0len = 0, z1len = 0, z2len = 0, z3len = 0, z4len = 0;
		int		/*sz0, */sz1, sz2, sz3/*, sz4*/;

		dig_t	*txm2, *tym2, *txm1, *tym1, *txp1, *typ1;
		len_t	txm2len = 0, tym2len = 0, txm1len = 0, tym1len = 0, txp1len = 0, typ1len = 0;
		int		stxm2 = 1, stym2 = 1, stxm1 = 1, stym1 = 1, stxp1 = 1, styp1 = 1;

		dig_t	*txym2, *txym1, *txyp1;
		len_t	txym2len = 0, txym1len = 0, txyp1len = 0;
		int		stxym2 = 1, stxym1 = 1, stxyp1 = 1;

		m = alen;
		if(m < blen)
		{
			m = blen;
		}
		m = (m+2)/3;

		__split3(a, alen, m, &x0, &x0len, &x1, &x1len, &x2, &x2len);
		__split3(b, blen, m, &y0, &y0len, &y1, &y1len, &y2, &y2len);

		// r0 = x0*y0
		z0 = c;
		__mul_toomcook3(x0, x0len, y0, y0len, z0, &z0len);
		*clen = z0len;

		// r4 = x_inf * y_inf
		z4 = bi_malloc(2*m, __LINE__);
		__mul_toomcook3(x2, x2len, y2, y2len, z4, &z4len);

		txm2 = bi_malloc(m+1, __LINE__);
		__eval_tc3(x0, x0len, x1, x1len, x2, x2len, -2, &stxm2, txm2, &txm2len);
		tym2 = bi_malloc(m+1, __LINE__);
		__eval_tc3(y0, y0len, y1, y1len, y2, y2len, -2, &stym2, tym2, &tym2len);
		txym2 = bi_malloc(2*(m+1), __LINE__);
		stxym2 = stxm2*stym2;
		__mul_toomcook3(txm2, txm2len, tym2, tym2len, txym2, &txym2len);
		bi_free(txm2);
		bi_free(tym2);

		txm1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(x0, x0len, x1, x1len, x2, x2len, -1, &stxm1, txm1, &txm1len);
		tym1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(y0, y0len, y1, y1len, y2, y2len, -1, &stym1, tym1, &tym1len);
		txym1 = bi_malloc(2*(m+1), __LINE__);
		stxym1 = stxm1*stym1;
		__mul_toomcook3(txm1, txm1len, tym1, tym1len, txym1, &txym1len);
		bi_free(txm1);
		bi_free(tym1);

		txp1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(x0, x0len, x1, x1len, x2, x2len, 1, &stxp1, txp1, &txp1len);
		typ1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(y0, y0len, y1, y1len, y2, y2len, 1, &styp1, typ1, &typ1len);
		txyp1 = bi_malloc(2*(m+1), __LINE__);
		stxyp1 = stxp1*styp1;
		__mul_toomcook3(txp1, txp1len, typ1, typ1len, txyp1, &txyp1len);
		bi_free(txp1);
		bi_free(typ1);

		// r3  = (r(−2) - r(1))/3
		z3 = bi_malloc(2*(m+1), __LINE__);
		memcpy(z3, txym2, (size_t)txym2len * sizeof(dig_t));
		z3len = txym2len;
		sz3 = stxym2;
		bi_free(txym2);
		__sadd(&sz3, z3, &z3len, -stxyp1, txyp1, txyp1len);
		__div_by_3(z3, &z3len);

		// r1 = (r(1) - r(−1))/2
		z1 = bi_malloc(2*(m+1), __LINE__);
		memcpy(z1, txyp1, (size_t)txyp1len * sizeof(dig_t));
		z1len = txyp1len;
		sz1 = stxyp1;
		bi_free(txyp1);
		__sadd(&sz1, z1, &z1len, -stxym1, txym1, txym1len);
		__div_by_2(z1, &z1len);

		// r2 = r(−1) - r0
		z2 = bi_malloc(2*(m+1), __LINE__);
		memcpy(z2, txym1, (size_t)txym1len * sizeof(dig_t));
		z2len = txym1len;
		sz2 = stxym1;
		bi_free(txym1);
		__sadd(&sz2, z2, &z2len, -1, z0, z0len);

		// r3 = (r2 - r3)/2 + r4 + r4
		sz3 = -sz3;
		__sadd(&sz3, z3, &z3len, sz2, z2, z2len);
		__div_by_2(z3, &z3len);
		__sadd(&sz3, z3, &z3len, 1, z4, z4len);
		__sadd(&sz3, z3, &z3len, 1, z4, z4len);

		// r2 = r2 + r1 - r4
		__sadd(&sz2, z2, &z2len, sz1, z1, z1len);
		__sadd(&sz2, z2, &z2len, -1, z4, z4len);

		// r1 = r1 - r3
		__sadd(&sz1, z1, &z1len, -sz3, z3, z3len);

		// c == z0 here
		bi_add_shift(c, clen, z1, z1len, 1*m);
		bi_free(z1);
		bi_add_shift(c, clen, z2, z2len, 2*m);
		bi_free(z2);
		bi_add_shift(c, clen, z3, z3len, 3*m);
		bi_free(z3);
		bi_add_shift(c, clen, z4, z4len, 4*m);
		bi_free(z4);
	}
}






#ifdef	PARAL_MULT

typedef struct
{
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	int		cnt;
} event;

static void		event_init(event* e)
{
	pthread_mutex_init(&e->lock, (pthread_mutexattr_t*)0);
	pthread_cond_init(&e->cond, (pthread_condattr_t*)0);
	e->cnt = 0;
}

static void		event_deinit(event* e)
{
	pthread_mutex_destroy(&e->lock);
	pthread_cond_destroy(&e->cond);
	e->cnt = 0;
}

static void		event_inc(event* e)
{
	pthread_mutex_lock(&e->lock);
	e->cnt++;
	pthread_cond_signal(&e->cond);
	pthread_mutex_unlock(&e->lock);
}

static void		event_dec(event* e)
{
	pthread_mutex_lock(&e->lock);
	while(e->cnt <= 0)
	{
		pthread_cond_wait(&e->cond, &e->lock);
	}
	e->cnt--;
	pthread_mutex_unlock(&e->lock);
}

static void		event_reset(event* e)
{
	pthread_mutex_lock(&e->lock);
	e->cnt = 0;
	pthread_mutex_unlock(&e->lock);
}

typedef struct
{
	int		state;	/* 0: empty, 1: ready, 2: computing, 3: finished */
	dig_t	*a, *b;
	dig_t	*r;
	len_t	alen, blen, rlen;
	int		eval_sign;
	int		free_ab;	
} mul_info;

static int	__g_bFinish = 0;
static pthread_t	__g_thr[MAX_PARAL_CNT];

static pthread_mutex_t	__g_lock;
static event	__g_event_put, __g_event_get;

static mul_info		__g_mi[MAX_PARAL_CNT];
static int	__g_mi_idx;

static void*	__mul_thread_proc(void* pParam)
{
	while(1)
	{
		int		i;
		mul_info*	mi = (mul_info*)0;

		event_dec(&__g_event_put);

		pthread_mutex_lock(&__g_lock);
		for(i = 0; i < (int)(sizeof(__g_mi)/sizeof(__g_mi[0])); i++)
		{
			if(1 == __g_mi[i].state)
			{
				mi = __g_mi + i;
				mi->state = 2;
				break;
			}
		}
		pthread_mutex_unlock(&__g_lock);

		if(mi)
		{
			__mul_toomcook3(mi->a, mi->alen, mi->b, mi->blen, mi->r, &(mi->rlen));
			//__mul_karatsuba(mi->a, mi->alen, mi->b, mi->blen, mi->r, &(mi->rlen));
			if(mi->free_ab)
			{
				bi_free(mi->a);
				bi_free(mi->b);
			}
			pthread_mutex_lock(&__g_lock);
			mi->state = 3;
			pthread_mutex_unlock(&__g_lock);

			event_inc(&__g_event_get);
		}
		else if(__g_bFinish)
		{
			break;
		}
	}
	return (void*)0;
}

static void		__mul_toomcook3_paral(
	int depth,
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen,
	dig_t* c, len_t* clen)
{
	// # of allocated digits of c must be greater than or equal to (alen+blen+1)
	if(UNLIKELY(alen <= 0 || blen <= 0))
	{
		*clen = 0;
		return;
	}

	if(UNLIKELY(blen < TC3_CUTOFF))
	{
		__mul_karatsuba(a, alen, b, blen, c, clen);
		return;
	}

	{
		len_t	m;
//		len_t	i;
		const dig_t	*x0, *x1, *x2;
		len_t	x0len = 0, x1len = 0, x2len = 0;
		const dig_t	*y0, *y1, *y2;
		len_t	y0len = 0, y1len = 0, y2len = 0;
		dig_t	*z0 = NULL, *z1, *z2, *z3, *z4 = NULL;
		len_t	z0len = 0, z1len = 0, z2len = 0, z3len = 0, z4len = 0;
		int		/*sz0, */sz1, sz2, sz3/*, sz4*/;

		dig_t	*txm2, *tym2, *txm1, *tym1, *txp1, *typ1;
		len_t	txm2len = 0, tym2len = 0, txm1len = 0, tym1len = 0, txp1len = 0, typ1len = 0;
		int		stxm2 = 1, stym2 = 1, stxm1 = 1, stym1 = 1, stxp1 = 1, styp1 = 1;

		dig_t	*txym2 = NULL, *txym1 = NULL, *txyp1 = NULL;
		len_t	txym2len = 0, txym1len = 0, txyp1len = 0;
		int		stxym2 = 1, stxym1 = 1, stxyp1 = 1;

		m = alen;
		if(m < blen)
		{
			m = blen;
		}
		m = (m+2)/3;

		__split3(a, alen, m, &x0, &x0len, &x1, &x1len, &x2, &x2len);
		__split3(b, blen, m, &y0, &y0len, &y1, &y1len, &y2, &y2len);

		// r0 = x0*y0
		if(PARAL_START_DEPTH != depth)
		{
			z0 = c;
			__mul_toomcook3_paral(depth+1, x0, x0len, y0, y0len, z0, &z0len);
			*clen = z0len;
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;

			while(1)
			{
				int	do_break = 0;

				pthread_mutex_lock(&__g_lock);
				if(3 == mi->state)
				{
					mi->state = 0;
					z0 = mi->r;
					z0len = mi->rlen;
					do_break = 1;
				}
				pthread_mutex_unlock(&__g_lock);
				if(do_break)
				{
					if(z0len > 0)
					{
						memcpy(c, z0, (size_t)z0len * sizeof(dig_t));
					}
					if(z0)
					{
						bi_free(z0);
					}
					z0 = c;
					*clen = z0len;
					break;
				}
				event_dec(&__g_event_get);
			}
		}

		// r4 = x_inf * y_inf
		if(PARAL_START_DEPTH != depth)
		{
			z4 = bi_malloc(2*m, __LINE__);
			__mul_toomcook3_paral(depth+1, x2, x2len, y2, y2len, z4, &z4len);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;

			while(1)
			{
				int	do_break = 0;

				pthread_mutex_lock(&__g_lock);
				if(3 == mi->state)
				{
					mi->state = 0;
					z4 = mi->r;
					z4len = mi->rlen;
					do_break = 1;
				}
				pthread_mutex_unlock(&__g_lock);
				if(do_break)
				{
					break;
				}
				event_dec(&__g_event_get);
			}
		}

		if(PARAL_START_DEPTH != depth)
		{
			txm2 = bi_malloc(m+1, __LINE__);
			__eval_tc3(x0, x0len, x1, x1len, x2, x2len, -2, &stxm2, txm2, &txm2len);
			tym2 = bi_malloc(m+1, __LINE__);
			__eval_tc3(y0, y0len, y1, y1len, y2, y2len, -2, &stym2, tym2, &tym2len);
			txym2 = bi_malloc(2*(m+1), __LINE__);
			stxym2 = stxm2*stym2;
			__mul_toomcook3_paral(depth+1, txm2, txm2len, tym2, tym2len, txym2, &txym2len);
			bi_free(txm2);
			bi_free(tym2);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;

			while(1)
			{
				int	do_break = 0;

				pthread_mutex_lock(&__g_lock);
				if(3 == mi->state)
				{
					mi->state = 0;
					txym2 = mi->r;
					txym2len = mi->rlen;
					stxym2 = mi->eval_sign;
					do_break = 1;
				}
				pthread_mutex_unlock(&__g_lock);
				if(do_break)
				{
					break;
				}
				event_dec(&__g_event_get);
			}
		}

		if(PARAL_START_DEPTH != depth)
		{
			txm1 = bi_malloc(m+1, __LINE__);
			__eval_tc3(x0, x0len, x1, x1len, x2, x2len, -1, &stxm1, txm1, &txm1len);
			tym1 = bi_malloc(m+1, __LINE__);
			__eval_tc3(y0, y0len, y1, y1len, y2, y2len, -1, &stym1, tym1, &tym1len);
			txym1 = bi_malloc(2*(m+1), __LINE__);
			stxym1 = stxm1*stym1;
			__mul_toomcook3_paral(depth+1, txm1, txm1len, tym1, tym1len, txym1, &txym1len);
			bi_free(txm1);
			bi_free(tym1);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;

			while(1)
			{
				int	do_break = 0;

				pthread_mutex_lock(&__g_lock);
				if(3 == mi->state)
				{
					mi->state = 0;
					txym1 = mi->r;
					txym1len = mi->rlen;
					stxym1 = mi->eval_sign;
					do_break = 1;
				}
				pthread_mutex_unlock(&__g_lock);
				if(do_break)
				{
					break;
				}
				event_dec(&__g_event_get);
			}
		}

		if(PARAL_START_DEPTH != depth)
		{
			txp1 = bi_malloc(m+1, __LINE__);
			__eval_tc3(x0, x0len, x1, x1len, x2, x2len, 1, &stxp1, txp1, &txp1len);
			typ1 = bi_malloc(m+1, __LINE__);
			__eval_tc3(y0, y0len, y1, y1len, y2, y2len, 1, &styp1, typ1, &typ1len);
			txyp1 = bi_malloc(2*(m+1), __LINE__);
			stxyp1 = stxp1*styp1;
			__mul_toomcook3_paral(depth+1, txp1, txp1len, typ1, typ1len, txyp1, &txyp1len);
			bi_free(txp1);
			bi_free(typ1);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;

			while(1)
			{
				int	do_break = 0;

				pthread_mutex_lock(&__g_lock);
				if(3 == mi->state)
				{
					mi->state = 0;
					txyp1 = mi->r;
					txyp1len = mi->rlen;
					stxyp1 = mi->eval_sign;
					do_break = 1;
				}
				pthread_mutex_unlock(&__g_lock);
				if(do_break)
				{
					break;
				}
				event_dec(&__g_event_get);
			}
		}

		// r3  = (r(−2) - r(1))/3
		z3 = bi_malloc(2*(m+1), __LINE__);
		memcpy(z3, txym2, (size_t)txym2len * sizeof(dig_t));
		z3len = txym2len;
		sz3 = stxym2;
		if(txym2)
		{
			bi_free(txym2);
		}
		__sadd(&sz3, z3, &z3len, -stxyp1, txyp1, txyp1len);
		__div_by_3(z3, &z3len);

		// r1 = (r(1) - r(−1))/2
		z1 = bi_malloc(2*(m+1), __LINE__);
		memcpy(z1, txyp1, (size_t)txyp1len * sizeof(dig_t));
		z1len = txyp1len;
		sz1 = stxyp1;
		if(txyp1)
		{
			bi_free(txyp1);
		}
		__sadd(&sz1, z1, &z1len, -stxym1, txym1, txym1len);
		__div_by_2(z1, &z1len);

		// r2 = r(−1) - r0
		z2 = bi_malloc(2*(m+1), __LINE__);
		memcpy(z2, txym1, (size_t)txym1len * sizeof(dig_t));
		z2len = txym1len;
		sz2 = stxym1;
		if(txym1)
		{
			bi_free(txym1);
		}
		__sadd(&sz2, z2, &z2len, -1, z0, z0len);

		// r3 = (r2 - r3)/2 + r4 + r4
		sz3 = -sz3;
		__sadd(&sz3, z3, &z3len, sz2, z2, z2len);
		__div_by_2(z3, &z3len);
		__sadd(&sz3, z3, &z3len, 1, z4, z4len);
		__sadd(&sz3, z3, &z3len, 1, z4, z4len);

		// r2 = r2 + r1 - r4
		__sadd(&sz2, z2, &z2len, sz1, z1, z1len);
		__sadd(&sz2, z2, &z2len, -1, z4, z4len);

		// r1 = r1 - r3
		__sadd(&sz1, z1, &z1len, -sz3, z3, z3len);

		// c == z0 here
		bi_add_shift(c, clen, z1, z1len, 1*m);
		bi_free(z1);
		bi_add_shift(c, clen, z2, z2len, 2*m);
		bi_free(z2);
		bi_add_shift(c, clen, z3, z3len, 3*m);
		bi_free(z3);
		bi_add_shift(c, clen, z4, z4len, 4*m);
		if(z4)
		{
			bi_free(z4);
		}
	}
}

static void		__mul_toomcook3_trigger(
	int depth,
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen)
{
	// # of allocated digits of c must be greater than or equal to (alen+blen+1)
	if(UNLIKELY(alen <= 0 || blen <= 0))
	{
		return;
	}

	if(UNLIKELY(blen < TC3_CUTOFF))
	{
		return;
	}

	{
		len_t	m;
		const dig_t	*x0, *x1, *x2;
		len_t	x0len = 0, x1len = 0, x2len = 0;
		const dig_t	*y0, *y1, *y2;
		len_t	y0len = 0, y1len = 0, y2len = 0;

		dig_t	*txm2, *tym2, *txm1, *tym1, *txp1, *typ1;
		len_t	txm2len = 0, tym2len = 0, txm1len = 0, tym1len = 0, txp1len = 0, typ1len = 0;
		int		stxm2 = 1, stym2 = 1, stxm1 = 1, stym1 = 1, stxp1 = 1, styp1 = 1;

		int		stxym2 = 1, stxym1 = 1, stxyp1 = 1;

		m = alen;
		if(m < blen)
		{
			m = blen;
		}
		m = (m+2)/3;

		__split3(a, alen, m, &x0, &x0len, &x1, &x1len, &x2, &x2len);
		__split3(b, blen, m, &y0, &y0len, &y1, &y1len, &y2, &y2len);

		// r0 = x0*y0
		if(PARAL_START_DEPTH != depth)
		{
			__mul_toomcook3_trigger(depth+1, x0, x0len, y0, y0len);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;
			int	do_enq = 0;

			pthread_mutex_lock(&__g_lock);
			if(x0len > 0 && y0len > 0)
			{
				dig_t *xx0, *yy0;
				xx0 = bi_malloc(x0len, __LINE__);
				memcpy(xx0, x0, (size_t)x0len * sizeof(dig_t));
				yy0 = bi_malloc(y0len, __LINE__);
				memcpy(yy0, y0, (size_t)y0len * sizeof(dig_t));

				mi->state = 1;
				mi->a = xx0;
				mi->alen = x0len;
				mi->b = yy0;
				mi->blen = y0len;
				mi->free_ab = 1;
				mi->r = bi_malloc(mi->alen + mi->blen, __LINE__);
				mi->rlen = 0;
				mi->eval_sign = 1;
				do_enq = 1;
			}
			else
			{
				mi->state = 3;
				mi->free_ab = 0;
				mi->r = (dig_t*)0;
				mi->rlen = 0;
				mi->eval_sign = 1;
			}
			pthread_mutex_unlock(&__g_lock);
			if(do_enq)
			{
				event_inc(&__g_event_put);
			}
		}

		// r4 = x_inf * y_inf
		if(PARAL_START_DEPTH != depth)
		{
			__mul_toomcook3_trigger(depth+1, x2, x2len, y2, y2len);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;
			int	do_enq = 0;

			pthread_mutex_lock(&__g_lock);
			if(x2len > 0 && y2len > 0)
			{
				dig_t *xx2, *yy2;
				xx2 = bi_malloc(x2len, __LINE__);
				memcpy(xx2, x2, (size_t)x2len * sizeof(dig_t));
				yy2 = bi_malloc(y2len, __LINE__);
				memcpy(yy2, y2, (size_t)y2len * sizeof(dig_t));

				mi->state = 1;
				mi->a = xx2;
				mi->alen = x2len;
				mi->b = yy2;
				mi->blen = y2len;
				mi->free_ab = 1;
				mi->r = bi_malloc(mi->alen + mi->blen, __LINE__);
				mi->rlen = 0;
				mi->eval_sign = 1;
				do_enq = 1;
			}
			else
			{
				mi->state = 3;
				mi->free_ab = 0;
				mi->r = (dig_t*)0;
				mi->rlen = 0;
				mi->eval_sign = 1;
			}
			pthread_mutex_unlock(&__g_lock);
			if(do_enq)
			{
				event_inc(&__g_event_put);
			}
		}

		txm2 = bi_malloc(m+1, __LINE__);
		__eval_tc3(x0, x0len, x1, x1len, x2, x2len, -2, &stxm2, txm2, &txm2len);
		tym2 = bi_malloc(m+1, __LINE__);
		__eval_tc3(y0, y0len, y1, y1len, y2, y2len, -2, &stym2, tym2, &tym2len);
		stxym2 = stxm2*stym2;
		if(PARAL_START_DEPTH != depth)
		{
			__mul_toomcook3_trigger(depth+1, txm2, txm2len, tym2, tym2len);
			bi_free(txm2);
			bi_free(tym2);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;
			int	do_enq = 0;

			pthread_mutex_lock(&__g_lock);
			if(txm2len > 0 && tym2len > 0)
			{
				mi->state = 1;
				mi->a = txm2;
				mi->alen = txm2len;
				mi->b = tym2;
				mi->blen = tym2len;
				mi->free_ab = 1;
				mi->r = bi_malloc(mi->alen + mi->blen, __LINE__);
				mi->rlen = 0;
				mi->eval_sign = stxym2;
				do_enq = 1;
			}
			else
			{
				bi_free(txm2);
				bi_free(tym2);
				mi->state = 3;
				mi->free_ab = 0;
				mi->r = (dig_t*)0;
				mi->rlen = 0;
				mi->eval_sign = 1;
			}
			pthread_mutex_unlock(&__g_lock);
			if(do_enq)
			{
				event_inc(&__g_event_put);
			}
		}

		txm1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(x0, x0len, x1, x1len, x2, x2len, -1, &stxm1, txm1, &txm1len);
		tym1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(y0, y0len, y1, y1len, y2, y2len, -1, &stym1, tym1, &tym1len);
		stxym1 = stxm1*stym1;
		if(PARAL_START_DEPTH != depth)
		{
			__mul_toomcook3_trigger(depth+1, txm1, txm1len, tym1, tym1len);
			bi_free(txm1);
			bi_free(tym1);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;
			int	do_enq = 0;

			pthread_mutex_lock(&__g_lock);
			if(txm1len > 0 && tym1len > 0)
			{
				mi->state = 1;
				mi->a = txm1;
				mi->alen = txm1len;
				mi->b = tym1;
				mi->blen = tym1len;
				mi->free_ab = 1;
				mi->r = bi_malloc(mi->alen + mi->blen, __LINE__);
				mi->rlen = 0;
				mi->eval_sign = stxym1;
				do_enq = 1;
			}
			else
			{
				bi_free(txm1);
				bi_free(tym1);
				mi->state = 3;
				mi->free_ab = 0;
				mi->r = (dig_t*)0;
				mi->rlen = 0;
				mi->eval_sign = 1;
			}
			pthread_mutex_unlock(&__g_lock);
			if(do_enq)
			{
				event_inc(&__g_event_put);
			}
		}

		txp1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(x0, x0len, x1, x1len, x2, x2len, 1, &stxp1, txp1, &txp1len);
		typ1 = bi_malloc(m+1, __LINE__);
		__eval_tc3(y0, y0len, y1, y1len, y2, y2len, 1, &styp1, typ1, &typ1len);
		stxyp1 = stxp1*styp1;
		if(PARAL_START_DEPTH != depth)
		{
			__mul_toomcook3_trigger(depth+1, txp1, txp1len, typ1, typ1len);
			bi_free(txp1);
			bi_free(typ1);
		}
		else
		{
			mul_info*	mi = __g_mi + __g_mi_idx++;
			int	do_enq = 0;

			pthread_mutex_lock(&__g_lock);
			if(txp1len > 0 && typ1len > 0)
			{
				mi->state = 1;
				mi->a = txp1;
				mi->alen = txp1len;
				mi->b = typ1;
				mi->blen = typ1len;
				mi->free_ab = 1;
				mi->r = bi_malloc(mi->alen + mi->blen, __LINE__);
				mi->rlen = 0;
				mi->eval_sign = stxyp1;
				do_enq = 1;
			}
			else
			{
				bi_free(txp1);
				bi_free(typ1);
				mi->state = 3;
				mi->free_ab = 0;
				mi->r = (dig_t*)0;
				mi->rlen = 0;
				mi->eval_sign = 1;
			}
			pthread_mutex_unlock(&__g_lock);
			if(do_enq)
			{
				event_inc(&__g_event_put);
			}
		}
	}
}

#endif





static void	__mul(const dig_t* a, len_t alen, const dig_t* b, len_t blen, dig_t* c, len_t* clen)
{
	// # of allocated digits of c must be greater than or equal to (alen+blen+1)
	if(UNLIKELY(alen <= 0 || blen <= 0))
	{
		*clen = 0;
		return;
	}

	if(UNLIKELY(blen < TC3_CUTOFF))
	{
		__mul_karatsuba(a, alen, b, blen, c, clen);
		return;
	}
#if	0
	if(3*blen <= alen)
	{
		dig_t*	t = bi_malloc(2*blen, __LINE__);
		len_t	ss = 0;

		*clen = 0;
		while(alen > 0)
		{
			len_t	l = (alen >= blen ? blen : alen);
			for(l--; l >= 0 && 0 == a[l]; l--)
			{
				;
			}
			l++;

			if(l > 0)
			{
				len_t	tlen;
				__mul(a, l, b, blen, t, &tlen);
				bi_add_shift(c, clen, t, tlen, ss);
			}
			a += blen;
			alen -= blen;
			ss += blen;
		}
		bi_free(t);
		return;
	}
#endif
#ifdef	PARAL_MULT
	if(__g_paral_cnt >= 2 && (alen >= 9*MIN_PARAL_BLK_SIZE || blen >= 9*MIN_PARAL_BLK_SIZE))
	{
		event_reset(&__g_event_put);
		event_reset(&__g_event_get);
		__g_mi_idx = 0;
		__mul_toomcook3_trigger(0, a, alen, b, blen);
		__g_mi_idx = 0;
		__mul_toomcook3_paral(0, a, alen, b, blen, c, clen);
	}
	else
	{
#endif
		__mul_toomcook3(a, alen, b, blen, c, clen);
#ifdef	PARAL_MULT
	}
#endif
}

void	bi_mul1(
	dig_t* a, len_t* alen,
	const dig_t* b, len_t blen)
{
	len_t	i, aalen = *alen;

	dig_t*	t = bi_malloc(aalen, __LINE__);
	len_t	tlen;

	for(i = 0; i < aalen; i++)
	{
		t[i] = a[i];
	}
	tlen = aalen;

	bi_mul2(t, tlen, b, blen, a, alen);

	bi_free(t);
}

void	bi_mul2(
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen,
	dig_t* c, len_t* clen)
{
	if(alen < blen)
	{
		const dig_t*	td;
		len_t	tl;
		td = a;
		a = b;
		b = td;
		tl = alen;
		alen = blen;
		blen = tl;
	}
	__mul(a, alen, b, blen, c, clen);
}

#if 0
static void pppp(const dig_t* n, len_t len)
{
	len_t	i;
	for(i = len-1; i >= 0; i--)
	//for(i = 0; i < len; i++)
	{
		printf("%08d ", n[i]);
	}
	printf("\n");
}
#endif

#if 0
/*
2 <= d < RADIX
*/
static void		__div2_one_digit(dig_t* a, len_t* palen, dig_t d, dig_t* q, len_t* pqlen)
{
	len_t	i, alen = *palen;
	ddig_t	dd;
	for(i = alen-1; i >= 0; i--)
	{
		if(alen-1 > i)
		{
			dd = (ddig_t)RADIX * a[alen-1] + a[i];
			q[i] = dd/d;
			dd -= (ddig_t)q[i]*d;
			if(dd > 0)
			{
				a[i] = dd%RADIX;
				alen--;
			}
			else
			{
				alen -= 2;
			}
		}
		else
		{
			dd = a[i];
			q[i] = dd/d;
			dd -= (ddig_t)q[i]*d;
			if(dd > 0)
			{
				a[i] = dd;
			}
			else
			{
				alen -= 1;
			}
		}
	}
	for(i = *palen-1; i >= 0 && !q[i]; i--)
	{
		;
	}
	*pqlen = i+1;
	*palen = alen;
}
#endif

/*
a > b
blen >= 2
capacity of a >= *palen+1
a, b can be rescaled.
*/
static void		__basic_div(
	dig_t* restrict a, len_t* restrict palen,
	const dig_t* restrict b, len_t blen,
	dig_t* restrict q, len_t* restrict pqlen)
{
	len_t i, j, gap;
	len_t alen = *palen;
	dig_t *aa;
	ddig_t divisor, m, dd/*, p10*/;
#if 0
    m = b[blen-1];
    while(m < RADIX/10)
    {
        m *= 10;
    }
    p10 = m/b[blen-1];

    if(p10 > 1)
    {
        __mul1_one_digit(a, &alen, p10);
        __mul1_one_digit(b, &blen, p10);
    }
#endif
	gap = alen - blen;

	divisor = b[blen-1];
	for(i = blen-2; i >= 0 && !b[i]; i--)
	{
		;
	}
	if(i >= 0)
	{
		divisor++;
	}

	for(i = gap; i >= 0; i--)
	{
		ddig_t ddig;

		aa = a + i;
		ddig = 0;
		while(__cmp_shift(a, alen, b, blen, i) >= 0)
		{
			if(alen > blen+i)
			{
				dd = (ddig_t)a[alen-1] * RADIX + a[alen-2];
			}
			else
			{
				dd = (ddig_t)a[alen-1];
			}
			m = dd/divisor;
			if(m <= 0)
			{
				m = 1;
			}

			dd = 0;
			for(j = 0; j < blen; j++)
			{
				dd += (ddig_t)aa[j] - m*b[j];
				aa[j] = dd % RADIX;
				if(aa[j] < 0)
				{
					aa[j] += RADIX;
					dd -= aa[j];
				}
				else
				{
				}
				dd /= RADIX;
			}
			for(; j+i < alen; j++)
			{
				dd += aa[j];
				aa[j] = dd % RADIX;
				if(aa[j] < 0)
				{
					aa[j] += RADIX;
					dd -= aa[j];
				}
				dd /= RADIX;
			}
			for(j = alen-1; j >= 0 && !a[j]; j--)
			{
				;
			}
			alen = j+1;
			ddig += m;
		}
		q[i] = (dig_t)ddig;
	}
	for(i = gap; i >= 0 && !q[i]; i--)
	{
		;
	}
	*pqlen = i+1;
#if 0
	if(p10 > 1 && alen > 0)
	{
		for(i = 0; i < alen-1; i++)
		{
			a[i] = (a[i+1]*(RADIX/p10))%RADIX + a[i]/p10;
		}
		a[i] /= p10;
		if(!a[i])
		{
			alen--;
		}
	}
	if(p10 > 1 && blen > 0)
	{
		for(i = 0; i < blen-1; i++)
		{
			((dig_t*)b)[i] = (b[i+1]*(RADIX/p10))%RADIX + b[i]/p10;
		}
		((dig_t*)b)[i] /= p10;
		if(!b[i])
		{
			blen--;
		}
	}
#endif
	*palen = alen;
}

#if 0
// 0 < b <= a
static void
__basic_div(
	dig_t* restrict a, len_t* restrict palen,
	const dig_t* restrict b, len_t blen,
	dig_t* restrict c, len_t* restrict pclen)
{
	dig_t* temp;
	dig_t* aa;
	len_t i, j, alen = *palen, aalen, templen, diff;

	temp = bi_malloc(blen+1, __LINE__);

	aalen = blen;
	diff = alen - blen;
	for(i = diff; i >= 0; i--)
	{
		ddig_t res_dig = 0;
		aa = a+i;
		while(1)
		{
			ddig_t ddig;
			int cmp = 0;

			if(aalen > blen)
			{
				cmp = 1;
				ddig = ( ((ddig_t)aa[blen])*RADIX + aa[blen-1] ) / (b[blen-1]+1);
			}
			else if(aalen == blen)
			{
				ddig = ((ddig_t)aa[blen-1]) / ( b[blen-1]+1 );
				if(ddig <= 0)
				{
					ddig = 1;
				}
				for(j = blen-1; j >= 0; j--)
				{
					if(aa[j] > b[j])
					{
						cmp = 1;
						break;
					}
					if(aa[j] < b[j])
					{
						cmp = -1;
						break;
					}
				}
			}
			else
			{
				ddig = 0;
				cmp = -1;
			}
			if(cmp < 0)
			{
				break;
			}

			// aa >= dig * b
			{
				ddig_t d = 0;

				templen = blen;
				for(j = 0; j < templen; j++)
				{
					d += ((ddig_t)b[j])*ddig;
					temp[j] = (dig_t)(d % RADIX);
					d /= RADIX;
				}
				if(d)
				{
					temp[templen++] = (dig_t)d;
				}
			}
			bi_sub1(aa, &aalen, temp, templen);
			res_dig += ddig;
		}
		aalen++;
		c[i] = (dig_t)res_dig;
	}
	bi_free(temp);
	for(i = diff; i >= 0 && !c[i]; i--)
	{
		;
	}
	*pclen = i+1;
	for(i = aalen-1; i >= 0 && a[i]; i--)
	{
		;
	}
	aalen = i+1;
}
#endif

#if 1
	#define NEWTON_RAPHSON_MIN_A_LEN 10000
	#define NEWTON_RAPHSON_MIN_AB_DIFF 1000
	#define MAX_BASIC_DIV_B_LEN 50
#else	// for test
	#define NEWTON_RAPHSON_MIN_A_LEN 1000
	#define NEWTON_RAPHSON_MIN_AB_DIFF 100
	#define MAX_BASIC_DIV_B_LEN 50
#endif

void	bi_div1(
	dig_t* a, len_t* palen,
	const dig_t* b, len_t blen,
	dig_t* q, len_t* pqlen)
{
	dig_t *y1, *y2, *tnum;
	len_t y1len, y2len, tnumlen;
	len_t alen = *palen, qlen;
	len_t i, j, prec;

	{
		int cmp = bi_cmp(a, alen, b, blen);
		if(cmp < 0)
		{
			*pqlen = 0;
			return;
		}
		if(cmp == 0)
		{
			q[0] = 1;
			*pqlen = 1;
			*palen = 0;
			return;
		}
	}

	if(alen < NEWTON_RAPHSON_MIN_A_LEN || blen < NEWTON_RAPHSON_MIN_AB_DIFF || alen - blen < NEWTON_RAPHSON_MIN_AB_DIFF )
	{
		__basic_div(a, palen, b, blen, q, pqlen);
		return;
	}

	// Now, 2 <= len(b) < len(a)

	prec = alen;

	y1 = bi_malloc(prec+1, __LINE__);
	y2 = bi_malloc(2*prec+1, __LINE__);
	tnum = bi_malloc(prec+1, __LINE__);

	// tnum <-- 1 * R^prec
	memset(tnum, 0, (size_t)prec * sizeof(dig_t));
	tnum[prec] = 1;
	tnumlen = prec+1;

	// Initial estimation of (1/b)*R^prec
	if(blen <= MAX_BASIC_DIV_B_LEN)
	{
		__basic_div(tnum, &tnumlen, b, blen, y1, &y1len);
	}
	else
	{
		dig_t tb[MAX_BASIC_DIV_B_LEN+1];
		len_t tblen, trunc_len = blen - MAX_BASIC_DIV_B_LEN;

		tblen = MAX_BASIC_DIV_B_LEN;
		memcpy(tb, b + trunc_len, (size_t)tblen * sizeof(dig_t));
		for(i = 0; i < trunc_len && !b[i]; i++) { ; }
		if(i < trunc_len)
		{
			bi_inc(tb, &tblen);
		}
		__basic_div(tnum, &tnumlen, tb, tblen, y1, &y1len);
		for(i = trunc_len, j = 0; i < y1len; i++, j++)
		{
			y1[j] = y1[i];
		}
		y1len = j;
	}
	// ( len(b) < len(a) ) ==> y1 > 0

	while(1)
	{
		// tnum <-- b*y1;
		bi_mul2(b, blen, y1, y1len, tnum, &tnumlen);

		// tnum = 2*R^prec - b*y1;
		for(i = 0; i < tnumlen && 0 == tnum[i]; i++)
		{
			;
		}
		if(i < prec)
		{
			tnum[i] = (dig_t)(RADIX - tnum[i]);
			i++;
			for(; i < tnumlen; i++)
			{
				tnum[i] = (dig_t)(RADIX-1 - tnum[i]);
			}
			for(; i < prec; i++)
			{
				tnum[i] = (dig_t)(RADIX-1);
			}
		}
		tnum[prec] = 1;
		tnumlen = prec+1;

		// y2 <-- y1*( 2*R^prec - b*y1 )
		bi_mul2(y1, y1len, tnum, tnumlen, y2, &y2len);

		// new approx. <= prev approx --> break
		if(bi_cmp(y2+prec, y2len-prec, y1, y1len) <= 0)
		{
			break;
		}

		// y1 <-- y1*( 2*R^prec - b*y1 ) * R^(-prec)
		y1len = y2len-prec;
		memcpy(y1, y2+prec, (size_t)y1len * sizeof(dig_t));
	}

	// y2 <-- a * { approx. (1/b)*R^prec }
	// y2 := { approx. (a/b) * R^prec }
	bi_mul2(a, alen, y1, y1len, y2, &y2len);

	// cc <-- { approx. (a/b) * R^prec } * R^(-prec)
	// cc := ( approx. a/b }
	qlen = y2len-prec;
	memcpy(q, y2+prec, (size_t)qlen * sizeof(dig_t));

	// y1 <-- b * ( approx. a/b }
	// y1 := { approx. a }
	bi_mul2(b, blen, q, qlen, y1, &y1len);

	// aa -= { approx. a }
	bi_sub1(a, &alen, y1, y1len);

	// At most once...
	while(bi_cmp(a, alen, b, blen) >= 0)
	{
		// aa -= b
		bi_sub1(a, &alen, b, blen);
		
		// cc += 1
		bi_inc(q, &qlen);
	}
	*palen = alen;
	*pqlen = qlen;

	bi_free(tnum);
	bi_free(y2);
	bi_free(y1);
}

void	bi_div2(
	const dig_t* a, len_t alen,
	const dig_t* b, len_t blen,
	dig_t* q, len_t* qlen,
	dig_t* r, len_t* rlen)
{
	len_t		i;

	dig_t*	rr = bi_malloc(alen, __LINE__);
	len_t		rrlen;

	memcpy(rr, a, (size_t)alen * sizeof(dig_t));
	rrlen = alen;

	bi_div1(rr, &rrlen, b, blen, q, qlen);

	if(r && rlen)
	{
		for(i = 0; i < rrlen; i++)
		{
			r[i] = rr[i];
		}
		*rlen = rrlen;
	}
	bi_free(rr);
}

static ddig_t	__simple_sqrt(ddig_t a)
{
	ddig_t	u = 0, v = RADIX, m, d;

	while(u <= v)
	{
		m = (u+v)/2;
		d = m*m;
		if(d < a)
		{
			u = m+1;
		}
		else if(d > a)
		{
			v = m-1;
		}
		else
		{
			return m;
		}
	}
	return v;
}

/*
	1. Calculate 1/sqrt(a) with y = x^(-2) - a
	2. Calculate sqrt(a) from a*(1/sqrt(a))

	Newton-Raphson iteration for y = x^(-2) - a
	    x <- x*(3-a*x^2)/2

	divide by 2 : Multiply by RADIX/2 and shift right by 1.

	No long division in iteration.
*/


/*
scale = 66;
sqrt(1208925819614629174706176);

bi_div1 에서 에러 났었음.
*/

// shift >= 0
// sqrt( a * RADIX^shift )
void	bi_sqrt(const dig_t* a, len_t alen, len_t shift, dig_t* r, len_t* prlen)
{
	dig_t *num1, *num2, *temp, *shell;
	len_t i, rlen, a_len, num1len, num2len, templen, shelllen;
	ddig_t dd, sqrt_dd;
	len_t trunc_len;

	if(alen <= 0)
	{
		*prlen = 0;
		return;
	}

	// Let A = aa * BASE^a_shift
	a_len = alen + shift; // length of A

	// Compose an integer <= { (1 / sqrt(A)) * BASE^a_len } as large as
	// possible.
	// First, compose an integer >= sqrt(A) as small as possible.

	if (a_len & 1) // odd
	{
		trunc_len = a_len - 1;
		dd = (ddig_t)a[alen - 1];
	}
	else
	{
		trunc_len = a_len - 2;
		if (alen >= 2)
		{
			dd = ((ddig_t) a[alen - 1]) * RADIX + ((ddig_t) a[alen - 2]);
		}
		else
		{
			dd = ((ddig_t) a[alen - 1]) * RADIX + ((ddig_t) 0);
		}
	}
	sqrt_dd = __simple_sqrt(dd);

	// a : small integer and scale is 0
	if (a_len <= 2)
	{
		r[0] = (dig_t)sqrt_dd;
		*prlen = 1;
		return;
	}

	// aa.len >= 3

	num1len = a_len/2 + 2;
	num2len = num1len + num1len;
	templen = a_len + num2len;
	// temp.len = a_len + a_len + 1 >= a_len + 4
	// Enough space for ( flr(sqrt(A)) + 1 )^2

	// 2*a_len + 1 + 2*aa.len + 1 + 1 + 1

	num1 = bi_malloc(num1len, __LINE__);
	num2 = bi_malloc(num2len, __LINE__);
	temp = bi_malloc(templen, __LINE__);

//printf("tempcap = %lu, num1cap = %lu, num2cap = %lu\n", temp.cap, num1.cap, num2.cap);

	num2len = trunc_len / 2;
	memset(num2, 0, (size_t)num2len * sizeof(dig_t));
	if (sqrt_dd * sqrt_dd < dd)
	{
		sqrt_dd += 1;
	}
	else
	{
		for (i = 0; i < trunc_len && !a[i]; i++)
		{
			;
		}
		if (i < trunc_len)
		{
			sqrt_dd += 1;
		}
	}
	if (sqrt_dd < RADIX)
	{
		num2[num2len++] = (dig_t)sqrt_dd;
	}
	else
	{
		num2[num2len++] = 0;
		num2[num2len++] = 1;
	}

	// BASE^a_len
	memset(temp, 0, (size_t)a_len * sizeof(dig_t));
	temp[a_len] = 1;
	templen = a_len + 1;

	// The only one long division.
	// Initial estimation of flr ( (1/sqrt(A)) * BASE^a_len )
//pppp(temp, templen);
//pppp(num2, num2len);
	bi_div1(temp, &templen, num2, num2len, num1, &num1len);
//pppp(num1, num1len);
//getchar();
//printf("a->len = %lu, a->cap = %lu, b->len = %lu, b->cap = %lu\n", a->len, a->cap, b->len, b->cap);

	while (1)
	{
		// x^2
		bi_mul2(num1, num1len, num1, num1len, num2, &num2len);

		// aa * x^2
		bi_mul2(a, alen, num2, num2len, temp, &templen);

		// ( aa * x^2 ) / BASE^(a_len - a_shift)
		// == ( aa * BASE^a_shift * x^2 ) / BASE^a_len
		// == A * x^2 * BASE^(-alen)
		num2len = templen - (a_len - shift);
		memcpy(num2, temp + (a_len - shift), (size_t)num2len * sizeof(dig_t));

		// 3*BASE^a_len - A*x^2*BASE^(-alen)
		for (i = 0; i < a_len && !num2[i]; i++)
		{
			;
		}
		if (i < a_len)
		{
			num2[i] = RADIX - num2[i];
			i++;
			for (; i < num2len; i++)
			{
				num2[i] = (RADIX - 1) - num2[i];
			}
			for (; i < a_len; i++)
			{
				num2[i] = RADIX - 1;
			}
		}
		num2[a_len] = 2;
		num2len = a_len + 1;

		// x*( 3*BASE^a_len - A*x^2*BASE^(-alen) )
		bi_mul2(num1, num1len, num2, num2len, temp, &templen);

		// ( x*( 3*BASE^a_len - A*x^2*BASE^(-alen) ) ) / 2
		__div_by_2(temp, &templen);

		// ( (x*(3*BASE^a_len - A*x^2*BASE^(-alen)))/2 ) * BASE^(-alen)
		shelllen = templen - a_len;
		shell = temp + a_len;

		// flr( ( y*(3*BASE^a_len - flr(A*y^2*BASE^(-a_len)) ) / 2 ) * BASE^(-a_len) )
		//
		// is equal to or greater by 1 than
		//
		// flr( ( y*(3*BASE^(2*a_len) - A*x^2 ) / 2 ) * BASE^(-2*a_len) )
		//
		// Proof --> sqrt_2.png
		// So, decrease 1
		bi_dec(shell, &shelllen);

		if (bi_cmp(shell, shelllen, num1, num1len) <= 0)
		{
			break;
		}

		num1len = shelllen;
		memcpy(num1, shell, (size_t)num1len * sizeof(dig_t));
	}

	// b <-- ( x * aa ) / BASE^(a_len - a_shift)
	// == ( aa * BASE^a_shift * x ) / BASE^a_len
	// == A * x * BASE^(-alen)
	// --> about sqrt(A)
	bi_mul2(a, alen, num1, num1len, temp, &templen);
	rlen = templen - (a_len - shift);
	memcpy(r, temp + (a_len - shift), (size_t)rlen * sizeof(dig_t));

	// Try b+1, b+2

	// temp1 = b^2
	bi_mul2(r, rlen, r, rlen, temp, &templen);

	while (1)
	{
		//  Actually, this block runs twice at most.

		int cmp;

		// b^2 + 2*b + 1 = (b+1)^2
		// temp1 <= (b+1)^2
		bi_add1(temp, &templen, r, rlen);
		bi_add1(temp, &templen, r, rlen);
		bi_inc(temp, &templen);

		// Compare A and (b+1)^2
		cmp = __cmp_shift(temp, templen, a, alen, shift);

		// A < (b+1)^2
		// return value is b
		if (cmp > 0)
		{
			break;
		}

		// A >= (b+1)^2
		// b <-- b+1
		bi_inc(r, &rlen);

		// Lucky. A == (b+1)^2
		if (cmp == 0)
		{
			break;
		}
	}
	*prlen = rlen;
	bi_free(temp);
	bi_free(num2);
	bi_free(num1);
}

void	bi_pow(const dig_t* a, len_t alen, len_t b, dig_t* c, len_t* clen)
{
	dig_t	*p1, *p2;
	len_t	p1len, p2len;

	if(UNLIKELY(alen <= 0))
	{
		*clen = 0;
		return;
	}
	c[0] = 1;
	*clen = 1;

	if(b <= 0)
	{
		return;
	}

	p1 = bi_malloc(alen*b + 1, __LINE__);
	p2 = bi_malloc(alen*b + 1, __LINE__);

	memcpy(p1, a, (size_t)alen * sizeof(dig_t));
	p1len = alen;

	while(1)
	{
		dig_t	*t;

		if(b&1)
		{
			bi_mul1(c, clen, p1, p1len);
		}
		b >>= 1;
		if(!b)
		{
			break;
		}
		bi_mul2(p1, p1len, p1, p1len, p2, &p2len);
		t = p1;
		p1 = p2;
		p2 = t;
		p1len = p2len;
	}
	bi_free(p1);
	bi_free(p2);
}

void	bi_fac(len_t u, len_t v, dig_t** n, len_t *nlen, len_t *nzeros)
{
	if(v-u < 100)
	{
		len_t	i;
		// 2^127 - 1 --> 39 digits in decimal.
		dig_t	*a, *aa, b[40];
		len_t	alen = (40*(v-u) + LOG10_RADIX-1) / LOG10_RADIX, blen, aalen;

		if(alen < 1)
		{
			alen = 1;
		}

		a = bi_malloc(alen, __LINE__);
		aa = bi_malloc(alen, __LINE__);
		a[0] = 1;
		alen = 1;

		for(i = u+1; i <= v; i++)
		{
			len_t	x = i;

			for(blen = 0; x > 0; blen++)
			{
				b[blen] = x % RADIX;
				x /= RADIX;
			}

			bi_elementary_mul(a, alen, b, blen, aa, &aalen);

			{
				dig_t	*t = a;
				a = aa;
				aa = t;
			}
			alen = aalen;
		}
		bi_free(aa);

		*n = a;
		*nlen = alen;
		if(nzeros)
		{
			*nzeros = 0;
		}
		return;
	}
/*
	if(v-u >= 100000)
	{
		fprintf(stderr, "++++ %10d, %10d, %10d\n", u, v, v-u);
	}
*/
	{
		dig_t	*a, *b, *r;
		len_t	alen, blen, azlen, bzlen, rlen, as = 0, bs = 0;
		len_t	m = (u+v)/2;

		bi_fac(u, m, &a, &alen, &azlen);
		for(as = 0; a[as] == 0; as++)
		{
			;
		}
		bi_fac(m, v, &b, &blen, &bzlen);
		for(bs = 0; b[bs] == 0; bs++)
		{
			;
		}
		if(nzeros)
		{
			r = bi_malloc(alen-as + blen-bs, __LINE__);
			bi_mul2(a+as, alen-as, b+bs, blen-bs, r, &rlen);
		}
		else
		{
			len_t	d = azlen+as + bzlen+bs;

			r = bi_malloc(alen + blen + d, __LINE__);
			bi_mul2(a+as, alen-as, b+bs, blen-bs, r+d, &rlen);
			memset(r, 0, (size_t)d * sizeof(dig_t));
			rlen += d;
		}
		bi_free(a);
		bi_free(b);
		*n = r;
		*nlen= rlen;
		if(nzeros)
		{
			*nzeros = azlen + as + bzlen + bs;
		}
	}
/*
	if(v-u >= 100000)
	{
		fprintf(stderr, "---- %10d, %10d, %10d\n", u, v, v-u);
	}
*/
}

len_t	bi_b10_len(const dig_t* a, len_t alen)
{
	dig_t	msb;
	int		i;
	len_t	dlen;

	// skip leading zeros.
	alen--;
	while(alen >= 0 && 0 == a[alen])
	{
		alen--;
	}
	alen++;

	if(alen <= 0)
	{
		return 0;
	}

	msb = a[alen-1];
	for(i = LOG10_RADIX-1; msb < g_pow10[i]; i--)
	{
		;
	}
	dlen = i+1;
	dlen += LOG10_RADIX*(alen-1);

	return dlen;
}

static int	__g_did_init = 0;

int		bi_init(int quiet)
{
	int		i;
	
	if(__g_did_init)
	{
		return 1;
	}

	g_pow10[0] = 1;
	for(i = 1; i < LOG10_RADIX; i++)
	{
		g_pow10[i] = 10*g_pow10[i-1];
	}

#ifdef	PARAL_MULT
	{
		char  *str;
		int   procs_on_system = (int)sysconf(_SC_NPROCESSORS_ONLN);

		str = getenv("BC_PARAL_CNT");
		if(str && 1 == sscanf(str, "%u", &__g_paral_cnt))
		{
			;
		}
		else
		{
			__g_paral_cnt = procs_on_system;
		}
		if(__g_paral_cnt < 1)
		{
			__g_paral_cnt = 1;
		}
		else if(__g_paral_cnt > procs_on_system)
		{
			__g_paral_cnt = procs_on_system;
		}
		if(!quiet && isatty(0) && isatty(1))
		{
			fprintf(stderr, "================================\n");
			if(str)
			{
				fprintf(stderr, " BC_PARAL_CNT = %d\n", __g_paral_cnt);
			}
			fprintf(stderr, " %d-threaded multiplication\n", __g_paral_cnt);
			fprintf(stderr, "================================\n");
		}
	}

	__g_bFinish = 0;

	if(__g_paral_cnt >= 2)
	{
		pthread_mutex_init(&__g_lock, NULL);
		event_init(&__g_event_put);
		event_init(&__g_event_get);

		for(i = 0; i < (int)(sizeof(__g_mi)/sizeof(__g_mi[0])); i++)
		{
			__g_mi[i].state = 0;
		}

		for(i = 0; i < __g_paral_cnt; i++)
		{
			pthread_create(&(__g_thr[i]), (pthread_attr_t*)0, __mul_thread_proc, NULL);
		}
	}
#endif

	__g_did_init = 1;
	return 1;
}

void	bi_deinit(void)
{
	if(!__g_did_init)
	{
		return;
	}

#ifdef	PARAL_MULT
	if(__g_paral_cnt >= 2)
	{
		int		i;

		__g_bFinish = 1;
		for(i = 0; i < __g_paral_cnt; i++)
		{
			event_inc(&__g_event_put);
		}
		for(i = 0; i < __g_paral_cnt; i++)
		{
			pthread_join(__g_thr[i], (void**)0);
		}

		event_deinit(&__g_event_get);
		event_deinit(&__g_event_put);
		pthread_mutex_destroy(&__g_lock);
	}
#endif

	__g_did_init = 0;
}



dig_t* bi_malloc(len_t n, int ln)
{
	if(n > 0)
	{
		dig_t* ptr;
		ptr = (dig_t*)malloc((size_t)n * sizeof(dig_t));
		if(ptr)
		{
			return ptr;
		}
		fprintf(stderr, "Out of memory!");
		exit(-1);
	}
	return (dig_t*)0;
}

void   bi_free(dig_t* ptr)
{
	if(ptr)
	{
		free(ptr);
	}
}

