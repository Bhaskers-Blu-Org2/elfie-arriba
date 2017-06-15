#pragma once

namespace V5
{
	namespace Query
	{
		public enum class Operator : char
		{
			Equals = 0,
			NotEquals = 1,
			LessThan = 2,
			LessThanOrEqual = 3,
			GreaterThan = 4,
			GreaterThanOrEqual = 5
		};
	}
}