#include "stdafx.h"
#include <intrin.h>
#include <nmmintrin.h>
#include "Operator.h"
#include "CompareToVector.h"

/*
  Parallel set comparison operations using AVX vector instructions, which compare 32 bytes in parallel.
  These can compare ~12GB/s per core.

  Instructions are only provided for signed values, and only for greater than and equals.
  Comparisons on unsigned types are done by subtracting first. 
	[If -128 is the lowest value, shift 0 to -128 so it'll compare as lowest].
  Other operators are done by swapping the operands.
    [ !(A > B) == (A <= B); !(A == B) == (A != B) ]

  __mm256_loadu_si128   - Load 32 bytes of unaligned data.

  __mm256_cmpgt_epi8    - Compare 32 bytes in parallel. Set a mask to 0x00 if A <= B or 0xFF if A > B
  __mm256_cmpeq_epi8    - Compare 32 bytes in parallel. Set a mask to 0x00 if A != B or 0xFF if A == B

  __mm256_movemask_epi8 - Set 32 bits to the first bit of each mask byte (convert byte mask to bit mask).

  __mm256_set1_epi8   - Set all bytes of a register to the same one byte value.
  __mm256_sub_epi8    - Subtract from each byte in parallel.

*/

#pragma unmanaged
public enum CompareOperatorN : char
{
	Equals = 0,
	NotEquals = 1,
	LessThan = 2,
	LessThanOrEqual = 3,
	GreaterThan = 4,
	GreaterThanOrEqual = 5
};

public enum BooleanOperatorN : char
{
	And = 0,
	AndNot = 1,
	Or = 2
};

public enum SigningN : char
{
	Unsigned = 0,
	Signed = 1
};

template<CompareOperatorN cOp, BooleanOperatorN bOp, SigningN sign>
static void Where(unsigned __int8* set, int length, unsigned __int8 value, unsigned __int64* matchVector)
{
	int i = 0;
	unsigned __int64 result;

	// Load a mask to convert unsigned values for signed comparison
	__m256i unsignedToSigned = _mm256_set1_epi8(-128);

	// Load copies of the value to compare against
	__m256i blockOfValue = _mm256_set1_epi8(value);
	if (sign == SigningN::Unsigned) blockOfValue = _mm256_sub_epi8(blockOfValue, unsignedToSigned);

	// Compare 64-byte blocks and generate a 64-bit result while there's enough data
	int blockLength = length - (length & 63);
	for (; i < blockLength; i += 64)
	{
		__m256i block1 = _mm256_loadu_si256((__m256i*)(&set[i]));
		__m256i block2 = _mm256_loadu_si256((__m256i*)(&set[i + 32]));

		if (sign == SigningN::Unsigned) block1 = _mm256_sub_epi8(block1, unsignedToSigned);
		if (sign == SigningN::Unsigned) block2 = _mm256_sub_epi8(block2, unsignedToSigned);

		__m256i matchMask1;
		__m256i matchMask2;
		switch (cOp)
		{
			case CompareOperatorN::GreaterThan:
			case CompareOperatorN::LessThanOrEqual:
				matchMask1 = _mm256_cmpgt_epi8(block1, blockOfValue);
				matchMask2 = _mm256_cmpgt_epi8(block2, blockOfValue);
				break;
			case CompareOperatorN::LessThan:
			case CompareOperatorN::GreaterThanOrEqual:
				matchMask1 = _mm256_cmpgt_epi8(blockOfValue, block1);
				matchMask2 = _mm256_cmpgt_epi8(blockOfValue, block2);
				break;
			case CompareOperatorN::Equals:
			case CompareOperatorN::NotEquals:
				matchMask1 = _mm256_cmpeq_epi8(block1, blockOfValue);
				matchMask2 = _mm256_cmpeq_epi8(block2, blockOfValue);
				break;
		}

		unsigned int matchBits1 = _mm256_movemask_epi8(matchMask1);
		unsigned int matchBits2 = _mm256_movemask_epi8(matchMask2);
		result = ((unsigned __int64)matchBits2) << 32 | matchBits1;

		if (cOp == CompareOperatorN::LessThanOrEqual || cOp == CompareOperatorN::GreaterThanOrEqual || cOp == CompareOperatorN::NotEquals)
		{
			result = ~result;
		}

		switch (bOp)
		{
			case BooleanOperatorN::And:
				matchVector[i >> 6] &= result;
				break;
			case BooleanOperatorN::Or:
				matchVector[i >> 6] |= result;
				break;
			case BooleanOperatorN::AndNot:
				matchVector[i >> 6] &= ~result;
				break;
		}
	}

	// Match remaining values individually
	if ((length & 63) > 0)
	{
		result = 0;

		for (; i < length; ++i)
		{
			switch (cOp)
			{
				case CompareOperatorN::GreaterThan:
					if (set[i] > value) result |= (0x1ULL << (i & 63));
					break;
				case CompareOperatorN::GreaterThanOrEqual:
					if (set[i] >= value) result |= (0x1ULL << (i & 63));
					break;
				case CompareOperatorN::LessThan:
					if (set[i] < value) result |= (0x1ULL << (i & 63));
					break;
				case CompareOperatorN::LessThanOrEqual:
					if (set[i] <= value) result |= (0x1ULL << (i & 63));
					break;
				case CompareOperatorN::Equals:
					if (set[i] == value) result |= (0x1ULL << (i & 63));
					break;
				case CompareOperatorN::NotEquals:
					if (set[i] != value) result |= (0x1ULL << (i & 63));
					break;
			}
		}

		switch (bOp)
		{
			case BooleanOperatorN::And:
				matchVector[i >> 6] &= result;
				break;
			case BooleanOperatorN::Or:
				matchVector[i >> 6] |= result;
				break;
			case BooleanOperatorN::AndNot:
				matchVector[i >> 6] &= ~result;
				break;
		}
	}
}

void CompareToVector::WhereGreaterThan(bool positive, bool and, unsigned __int8* set, int length, unsigned __int8 value, unsigned __int64* matchVector)
{
	Where<CompareOperatorN::GreaterThan, BooleanOperatorN::And, SigningN::Unsigned>(set, length, value, matchVector);
}

void CompareToVector::WhereLessThan(bool positive, bool and, unsigned __int8* set, int length, unsigned __int8 value, unsigned __int64* matchVector)
{
	Where<CompareOperatorN::LessThan, BooleanOperatorN::And, SigningN::Unsigned>(set, length, value, matchVector);
}

void CompareToVector::WhereEquals(bool positive, bool and, unsigned __int8* set, int length, unsigned __int8 value, unsigned __int64* matchVector)
{
	Where<CompareOperatorN::Equals, BooleanOperatorN::And, SigningN::Unsigned>(set, length, value, matchVector);
}