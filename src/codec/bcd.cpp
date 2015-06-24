#include <assert.h>
#include <string.h>

#if defined(__linux__) || defined(__MACH__)
	#include <alloca.h>
	#define _alloca alloca
#else
	#include <malloc.h>
#endif

#include "bcd.h"

#define _alloca alloca

namespace fast_codec
{
	// C������ ����������
	typedef enum {
		ROUND_NEAREST,  // � ����������  ( 12.4=>12 12.5=>13 12.6=>13 -12.4=>-12 12.5=>-13 -12.6=>-13 )
		ROUND_UP,       // ����� ( 12.4=>13 12.6=>13 -12.4=>-12 -12.6=>-12 )
		ROUND_DOWN,     // ���� ( 12.4=>12 12.6=>12 -12.4=>-13 -12.6=>-13 )
		ROUND_FROMZERO, // ( 12.4=>13 12.6=>13 -12.4=>-13 -12.6=>-13 )
		ROUND_TRUNC     // ��������� ������ ����� (���������� � ����) ( 12.4=>12 12.6=>12 -12.4=>-12 -12.6=>-12 )
	} roundstyle;

	#define BCD_MAX_DIGITS 63
	#define FIRSTNANBYTE 101

	// ���������� ������� �����, �.�. ��������� ������� ��������� �����. (1..9 -> 0;  10..99 -> 1, 0.01..0.09 -> -2 0 -> -128)
	int higher_dig_pos(const void* bcd_, BCD_TYPE type)
	{
		const uint8_t *bcd = (const uint8_t*)bcd_;
		int i = BCD_SIZEOF(type);
		int nfrac = BCD_TYPE_M(type);
		while (--i >= 0)
		{
			if (*bcd & 0x7F)
			{
				return i * 2 + ((*bcd & 0x7F)>9) - nfrac - (nfrac & 1);
			}
			++bcd;
		}
		return -BCD_MAX_DIGITS - 2;
	}

	static bool isNull(const void* p_, int sz)
	{
		const uint8_t *&p = *(const uint8_t**)&p_;
	
		while (sz--)
		{
			if (*p++)
				return false;
		}
		return true;
	}

	// ���������� ����� 0 - �������, 1 - �������, ... , -1 - �������, -2 - �����, ...
	int getdigit_internal(const void* bcd, BCD_TYPE type, int n)
	{
		int nfrac = BCD_TYPE_M(type);
		int nall = BCD_TYPE_N(type);
		int nint = nall - nfrac;
		int bint = (nint + 1) >> 1;
		if (n >= nint || n < -nfrac)
		{
			return 0;
		}
		int tbyte = *((const uint8_t*)bcd + bint - 1 - (n >> 1)) & 0x7F;
		return n % 2 ? tbyte / 10 : tbyte % 10;
	}

	// ���������� 0 ���� � ������� (�������) ����� ���������� ��� �����, � 1 ���� ������ ����,
	// �.�. ���� overhead ����� ������� ������
	inline int _fst_dig(BCD_TYPE t)
	{
		return (BCD_TYPE_N(t) - BCD_TYPE_M(t)) & 1;
	}

	// ��������� �����, �� ������� �������� �� ����
	uint32_t _addtodigit(uint8_t* dest, BCD_TYPE desttype, int ndigit)
	{
		int i = ndigit + _fst_dig(desttype);
		int carry = (i % 2) ? 1 : 10;
		int t;
		for (i /= 2; i > 0; --i)
		{
			t = *(dest + i) + carry;
			if (t >= 100)
			{
				*(dest + i) = t - 100;
				carry = 1;
			}
			else
			{
				*(dest + i) = t;
				carry = 0;
				break; // for( i
			}
		}
		t = *dest;
		uint8_t sign = t & 128;
		if (carry)
		{
			assert(i == 0);
			//int t = *dest;
			//BYTE sign = t & 128;
			t = (t & 127) + carry;
			if (t >= 100 || (_fst_dig(desttype) && t >= 10))
			{
				*(uint8_t*)dest = FIRSTNANBYTE;
				return BCD_RESULT_OVERFLOW | BCD_RESULT_BAD;
			}
			*dest = t | sign;
		}
		return sign ? BCD_RESULT_NEGATIVE : 0;
	}

	bool s_isNull(const void* p_, int sz)
	{
		const uint8_t *&p = *(const uint8_t**)&p_;

		assert(sz > 0);

		while (--sz >= 0)
		{
			if ((*p++) & 0x7F)
				return false;
		}
		return true;
	}

	uint32_t round_internal(void* bcd_, BCD_TYPE type, int fd, roundstyle rs)
	{
		uint8_t *bcd = (uint8_t*)bcd_;
		int nfrac = BCD_TYPE_M(type);
		int nall = BCD_TYPE_N(type);
		int nint = nall - nfrac;
		int bfrac = (nfrac + 1) >> 1;
		int bint = (nint + 1) >> 1;
		int ball = bint + bfrac;
		bool isNeg = (*bcd & 0x80) != 0;

		if (isNull(bcd, ball))
			return BCD_RESULT_ZERO;

		if (fd >= nfrac)
		{
			if (isNeg)
				return BCD_RESULT_NEGATIVE;
			assert(!isNull(bcd, ball));
			return 0;
		}

		if (-fd >= nint)  // ���� �����������, ��� ���-�� ������� ��������� �� ������ ������� �����
		{                  // �/��� ������ ����� ��������� ������ ������� �����, ����� ����� ���������
			switch (rs)
			{
			default:
				assert(!"Wrong roundstyle parameter");
			case ROUND_NEAREST:
				if (getdigit_internal(bcd, type, -1 - fd) >= 5)
				{
					*(uint8_t*)bcd_ = FIRSTNANBYTE;
					return BCD_RESULT_BAD | BCD_RESULT_OVERFLOW;
				}
				// else
				memset(bcd, 0, ball);
				return BCD_RESULT_ZERO | BCD_RESULT_PREC;
			case ROUND_UP:
				if (!isNeg)
				{
					*(uint8_t*)bcd_ = FIRSTNANBYTE;
					return BCD_RESULT_BAD | BCD_RESULT_OVERFLOW | BCD_RESULT_NEGATIVE;
				}
				// else
				memset(bcd, 0, ball);
				return BCD_RESULT_ZERO | BCD_RESULT_PREC;
			case ROUND_DOWN:
				if (isNeg)
				{
					*(uint8_t*)bcd_ = FIRSTNANBYTE;
					return BCD_RESULT_BAD | BCD_RESULT_OVERFLOW | BCD_RESULT_NEGATIVE;
				}
				// else
				memset(bcd, 0, ball);
				return BCD_RESULT_ZERO | BCD_RESULT_PREC;
			case ROUND_FROMZERO:
				*(uint8_t*)bcd_ = FIRSTNANBYTE;
				return BCD_RESULT_BAD | BCD_RESULT_OVERFLOW;
			case ROUND_TRUNC:
				memset(bcd, 0, ball);
				return BCD_RESULT_ZERO | BCD_RESULT_PREC;
			}
		}

		// -fd < nint && fd < nfrac
		int firstLostDigit = getdigit_internal(bcd, type, -1 - fd);

		// ���� ��� �����, ������� ���� ���������, ����, ������ ������ �� ����
		if ((firstLostDigit == 0) && isNull(bcd + bint + ((fd + 1) >> 1), bfrac - ((fd + 1) >> 1)))
		{
			return isNeg ? BCD_RESULT_NEGATIVE : 0;
		}

		bool add1; // ��������� ����� �� ����, �.�. � ������� ���������� ����� ��������� �������

		switch (rs)
		{
		default:
			assert(!"Wrong roundstyle parameter");
		case ROUND_NEAREST:
			add1 = firstLostDigit >= 5;
			break;
		case ROUND_UP:
			add1 = !isNeg;
			break;
		case ROUND_DOWN:
			add1 = isNeg;
			break;
		case ROUND_FROMZERO:
			add1 = true;
			break;
		case ROUND_TRUNC:
			add1 = false;
			break;
		}

		// �������� ������������� �����
		memset(bcd + bint + ((fd + 1) >> 1), 0, bfrac - ((fd + 1) >> 1));
		if (fd % 2)
		{
			int tmp_i = bint + ((fd - 1) >> 1);
			bcd[tmp_i] = ((bcd[tmp_i] & 0x7F) / 10) * 10 + (bcd[tmp_i] & 0x80);
		}

		if (add1)
		{
			return _addtodigit(bcd, type, nint + fd - 1) | BCD_RESULT_PREC;
		}
		// else
		if (s_isNull(bcd, ball))
		{
			bcd[0] = 0; //������ ���� (���� ���)
			return BCD_RESULT_ZERO | BCD_RESULT_PREC;
		}
		// else
		return isNeg ? BCD_RESULT_NEGATIVE | BCD_RESULT_PREC : BCD_RESULT_PREC;
	}

	uint32_t copy_internal(void* dst_, BCD_TYPE dstType, const void* src_, BCD_TYPE srcType, roundstyle rs)
	{
		assert(dst_ != src_);
		uint8_t *dst = (uint8_t*)dst_;
		const uint8_t *src = (const uint8_t*)src_;
		int dst_nfrac = BCD_TYPE_M(dstType);
		int dst_nall = BCD_TYPE_N(dstType);
		int dst_nint = dst_nall - dst_nfrac;

		int hi_srcdig = higher_dig_pos(src, srcType);
		if (hi_srcdig <= -BCD_MAX_DIGITS)
		{
			memset(dst, 0, BCD_SIZEOF(dstType));
			return BCD_RESULT_ZERO;
		}
		int t_src_bint = (hi_srcdig >> 1) + 1; // ���������� ��������� ���� � ����� �����

		if (dst_nint <= hi_srcdig)
		{
			*(uint8_t*)dst = FIRSTNANBYTE;
			return BCD_RESULT_OVERFLOW | BCD_RESULT_BAD;
		}

		int src_nfrac = BCD_TYPE_M(srcType);
		int src_nall = BCD_TYPE_N(srcType);
		int src_nint = src_nall - src_nfrac;

		int dst_bint = (dst_nint + 1) >> 1;
		if (src_nfrac <= dst_nfrac) // ��������� ���������� � ���������, ��������� �� ����
		{
			memset(dst, 0, dst_bint + ((dst_nfrac + 1) >> 1));
			int src_disp = ((src_nint + 1) >> 1) - t_src_bint;
			int dst_disp = ((dst_nint + 1) >> 1) - t_src_bint;
			memcpy(dst + dst_disp, src + src_disp, ((src_nfrac + 1) >> 1) + t_src_bint);
			if (*src & 0x80) // ������������� �����
			{
				dst[dst_disp] &= 0x7F;
				*dst |= 0x80;
				return BCD_RESULT_NEGATIVE;
			}
			// else
			return 0;
		}

		// ����� ������� ���� ��������, ����� ����������,
		// ��� ���������� �������� ���������� ������� (���������� ���� � ����� �����)
		int& tmp_nint = dst_nint;
		int& tmp_nfrac = src_nfrac;
		int tmp_nall = tmp_nint + tmp_nfrac;
		BCD_TYPE tmpType = BCD_MAKE_TYPE(tmp_nall, tmp_nfrac);

		assert((unsigned int)(((dst_nint + 1) >> 1) + ((tmp_nfrac + 1) >> 1)) == BCD_SIZEOF(tmpType)); // ���, �� ������ ������
		int tmp_ball = ((dst_nint + 1) >> 1) + ((tmp_nfrac + 1) >> 1);
		uint8_t *tmpBcd = (uint8_t*)_alloca(tmp_ball);

		int tmpDisp = ((tmp_nint + 1) >> 1) - t_src_bint;
		memset(tmpBcd, 0, tmpDisp);
		assert((unsigned int)(tmp_ball - tmpDisp) == BCD_SIZEOF(srcType) - ((src_nint + 1) >> 1) + t_src_bint);
		memcpy(tmpBcd + tmpDisp, src + ((src_nint + 1) >> 1) - t_src_bint, tmp_ball - tmpDisp);
		if (*src & 0x80) // source is negative
		{
			tmpBcd[tmpDisp] &= 0x7F;
			*tmpBcd |= 0x80;
		}
		uint32_t ret = round_internal(tmpBcd, tmpType, dst_nfrac, rs);
		if (ret & BCD_RESULT_BAD)
			*(uint8_t*)dst_ = FIRSTNANBYTE;
		else
			memcpy(dst_, tmpBcd, BCD_SIZEOF(dstType));
		return ret;
	}

	uint32_t decimal_to_bcd(uint8_t* bcd, BCD_TYPE type, mantissa_t intpart, exponent_t scale)
	{
		uint8_t tmpBcd[12]; // ������������ ���-�� ���������� ���� � UINT64 - 20, ������ ���������� � 11 ����. 12 - ��� �������� :-)
		uint8_t* p = tmpBcd + sizeof(tmpBcd);
		uint32_t tmp;
		if (scale % 2)
		{
			// �������� � tmp ������� 9 (�������� ���-��) ����
			if (intpart < 1000000000)
			{
				tmp = (int)intpart;
				intpart = 0;
			}
			else
			{
				tmp = (int)(intpart % 1000000000);
				intpart /= 1000000000;
			}

			// ������ ������� �� ��� � tmpBcd
			*--p = (tmp % 10) * 10;
			tmp /= 10;
		}
		else
		{
			// �������� � tmp ������� 8 (������ ���-��) ����
			if (intpart < 100000000)
			{
				tmp = (int)intpart;
				intpart = 0;
			}
			else
			{
				tmp = (int)(intpart % 100000000);
				intpart /= 100000000;
			}
		}

		// ������ � tmp 8 ����
		if (intpart)
		{
			int i = 4; // ������� ���� ���� ��������� ������� �� tmp
			while (tmp >= 100)
			{
				*--p = tmp % 100;
				tmp /= 100;
				--i;
			}
			*--p = tmp;
			--i;
			while (--i >= 0)
				*--p = 0;

			if (intpart < 1000000000) // ��� 9 �����, ��� �� ������
			{
				tmp = (int)intpart;
				while (tmp >= 100)
				{
					*--p = tmp % 100;
					tmp /= 100;
				}
				*--p = tmp;
			}
			else
			{
				tmp = (int)(intpart % 100000000); // � ��� 8 �����
				i = 4; // ������� ���� ���� ��������� ������� �� tmp
				while (tmp >= 100)
				{
					*--p = tmp % 100;
					tmp /= 100;
					--i;
				}
				*--p = tmp;
				--i;
				while (--i >= 0)
				{
					*--p = 0;
				}
				tmp = (int)(intpart / 100000000);
				while (tmp >= 100)
				{
					*--p = tmp % 100;
					tmp /= 100;
				}
				*--p = tmp;
			}
		}
		else // intpart == 0 �.�. ������ ���� ��� ����� ��� ��� � tmp
		{
			while (tmp >= 100)
			{
				*--p = tmp % 100;
				tmp /= 100;
			}
			*--p = tmp;
		}
		return copy_internal(bcd, type, p, BCD_MAKE_TYPE((BCD_TYPE)(sizeof(tmpBcd) + tmpBcd - p) * 2 - (scale & 1), scale), ROUND_TRUNC);
	}

	void bcd_to_decimal(const uint8_t* bcd, mantissa_t& intpart, exponent_t& pscale)
	{
		if (!bcd)
			return;

		int scale = *bcd++;
		int total = *bcd++;
		bool is_negative = (*bcd & 0x80) != 0;
		int expected_zeroes = total - 18;

		if (expected_zeroes < 0)
			expected_zeroes = 0;

		int digits = total/2 + ((total | scale) & 1);
		bcd = bcd + digits - 1;

		int64_t result = 0;
		int64_t multiplier = 1;

		for (int i = 0; i < digits; i++)
		{
			if (i > 9)
			{
				unsigned char val = ((*bcd--) & 0x7f);
				if (val != 0)
				{
					// Overflow error
					return;
				}
			}
			else
			{
				if (i == 0 && (scale % 2) != 0)
				{
					result = result + (int64_t)((*bcd--) & 0x7f) / 10;
					multiplier *= 10;
				}
				else
				{
					result = result + multiplier * (int64_t)((*bcd--) & 0x7f);
					multiplier *= 100;
				}
			}
		}

		if (is_negative)
			result = -result;

		intpart = result;
		pscale = scale;
	}

} // namespace
