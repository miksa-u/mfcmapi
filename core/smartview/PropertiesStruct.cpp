#include <core/stdafx.h>
#include <core/smartview/PropertiesStruct.h>

namespace smartview
{
	void PropertiesStruct::parse()
	{
		// For consistancy with previous parsings, we'll refuse to parse if asked to parse more than _MaxEntriesSmall
		// However, we may want to reconsider this choice.
		if (m_MaxEntries > _MaxEntriesSmall) return;

		DWORD dwPropCount = 0;

		// If we have a non-default max, it was computed elsewhere and we do expect to have that many entries. So we can reserve.
		if (m_MaxEntries != _MaxEntriesSmall)
		{
			m_Props.reserve(m_MaxEntries);
		}

		for (;;)
		{
			if (dwPropCount >= m_MaxEntries) break;
			auto sPropValueStruct = std::make_shared<SPropValueStruct>(dwPropCount++, m_NickName, m_RuleCondition);
			const auto curOffset = parser->getOffset();
			if (sPropValueStruct)
			{
				sPropValueStruct->block::parse(parser, false);
				m_Props.emplace_back(sPropValueStruct);
			}

			// If we parsed nothing, we're done.
			if (parser->getOffset() == curOffset)
			{
				break;
			}
		}
	}

	void PropertiesStruct::parseBlocks()
	{
		for (const auto& prop : m_Props)
		{
			addChild(prop);
		}
	}
} // namespace smartview