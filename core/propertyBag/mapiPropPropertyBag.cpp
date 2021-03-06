#include <core/stdafx.h>
#include <core/propertyBag/mapiPropPropertyBag.h>
#include <core/sortlistdata/sortListData.h>
#include <core/utility/registry.h>
#include <core/utility/output.h>
#include <core/mapi/mapiFunctions.h>
#include <core/utility/error.h>

namespace propertybag
{
	mapiPropPropertyBag::mapiPropPropertyBag(LPMAPIPROP lpProp, sortlistdata::sortListData* lpListData)
	{
		m_lpListData = lpListData;
		m_lpProp = lpProp;
		if (m_lpProp) m_lpProp->AddRef();
	}

	mapiPropPropertyBag::~mapiPropPropertyBag()
	{
		if (m_lpProp) m_lpProp->Release();
	}

	propBagFlags mapiPropPropertyBag::GetFlags() const
	{
		auto ulFlags = propBagFlags::None;
		if (m_bGetPropsSucceeded) ulFlags |= propBagFlags::BackedByGetProps;
		return ulFlags;
	}

	bool mapiPropPropertyBag::IsEqual(const std::shared_ptr<IMAPIPropertyBag> lpPropBag) const
	{
		if (!lpPropBag) return false;
		if (GetType() != lpPropBag->GetType()) return false;

		const auto lpOther = std::dynamic_pointer_cast<mapiPropPropertyBag>(lpPropBag);
		if (lpOther)
		{
			if (m_lpListData != lpOther->m_lpListData) return false;
			if (m_lpProp != lpOther->m_lpProp) return false;
			return true;
		}

		return false;
	}

	_Check_return_ HRESULT mapiPropPropertyBag::Commit()
	{
		if (nullptr == m_lpProp) return S_OK;

		return WC_H(m_lpProp->SaveChanges(KEEP_OPEN_READWRITE));
	}

	_Check_return_ HRESULT mapiPropPropertyBag::GetAllProps(ULONG FAR* lpcValues, LPSPropValue FAR* lppPropArray)
	{
		if (nullptr == m_lpProp) return S_OK;
		auto hRes = S_OK;
		m_bGetPropsSucceeded = false;

		if (!registry::useRowDataForSinglePropList)
		{
			const auto unicodeFlag = registry::preferUnicodeProps ? MAPI_UNICODE : fMapiUnicode;

			hRes = mapi::GetPropsNULL(m_lpProp, unicodeFlag, lpcValues, lppPropArray);
			if (SUCCEEDED(hRes))
			{
				m_bGetPropsSucceeded = true;
			}
			if (hRes == MAPI_E_CALL_FAILED)
			{
				// Some stores, like public folders, don't support properties on the root folder
				output::DebugPrint(output::dbgLevel::Generic, L"Failed to get call GetProps on this object!\n");
			}
			else if (FAILED(hRes)) // only report errors, not warnings
			{
				CHECKHRESMSG(hRes, IDS_GETPROPSNULLFAILED);
			}

			return hRes;
		}

		if (!m_bGetPropsSucceeded && m_lpListData)
		{
			*lpcValues = m_lpListData->cSourceProps;
			*lppPropArray = m_lpListData->lpSourceProps;
			hRes = S_OK;
		}

		return hRes;
	}

	_Check_return_ HRESULT mapiPropPropertyBag::GetProps(
		LPSPropTagArray lpPropTagArray,
		ULONG ulFlags,
		ULONG FAR* lpcValues,
		LPSPropValue FAR* lppPropArray)
	{
		if (nullptr == m_lpProp) return S_OK;

		return WC_H(m_lpProp->GetProps(lpPropTagArray, ulFlags, lpcValues, lppPropArray));
	}

	_Check_return_ HRESULT mapiPropPropertyBag::GetProp(ULONG ulPropTag, LPSPropValue FAR* lppProp)
	{
		if (nullptr == m_lpProp) return S_OK;

		auto hRes = WC_MAPI(mapi::HrGetOnePropEx(m_lpProp, ulPropTag, fMapiUnicode, lppProp));

		// Special case for profile sections and row properties - we may have a property which was in our row that isn't available on the object
		// In that case, we'll get MAPI_E_NOT_FOUND, but the property will be present in m_lpListData->lpSourceProps
		// So we fetch it from there instead
		// The caller will assume the memory was allocated from them, so copy before handing it back
		if (hRes == MAPI_E_NOT_FOUND && m_lpListData)
		{
			const auto lpProp = PpropFindProp(m_lpListData->lpSourceProps, m_lpListData->cSourceProps, ulPropTag);
			if (lpProp)
			{
				hRes = WC_MAPI(ScDupPropset(1, lpProp, MAPIAllocateBuffer, lppProp));
			}
		}

		return hRes;
	}

	void mapiPropPropertyBag::FreeBuffer(LPSPropValue lpProp)
	{
		// m_lpListData->lpSourceProps is the only data we might hand out that we didn't allocate
		// Don't delete it!!!
		if (m_lpListData && m_lpListData->lpSourceProps == lpProp) return;

		if (lpProp) MAPIFreeBuffer(lpProp);
	}

	_Check_return_ HRESULT mapiPropPropertyBag::SetProps(ULONG cValues, LPSPropValue lpPropArray)
	{
		if (nullptr == m_lpProp) return S_OK;

		LPSPropProblemArray lpProblems = nullptr;
		const auto hRes = WC_H(m_lpProp->SetProps(cValues, lpPropArray, &lpProblems));
		EC_PROBLEMARRAY(lpProblems);
		MAPIFreeBuffer(lpProblems);
		return hRes;
	}

	_Check_return_ HRESULT mapiPropPropertyBag::SetProp(LPSPropValue lpProp)
	{
		if (nullptr == m_lpProp) return S_OK;

		return WC_H(HrSetOneProp(m_lpProp, lpProp));
	}

	_Check_return_ HRESULT mapiPropPropertyBag::DeleteProp(ULONG ulPropTag)
	{
		if (nullptr == m_lpProp) return S_OK;

		const auto hRes = mapi::DeleteProperty(m_lpProp, ulPropTag);
		if (hRes == MAPI_E_NOT_FOUND && PROP_TYPE(ulPropTag) == PT_ERROR)
		{
			// We failed to delete a property without giving a type.
			// If the original type was error, it was quite likely a stream property.
			// Let's guess some common stream types.
			if (SUCCEEDED(mapi::DeleteProperty(m_lpProp, CHANGE_PROP_TYPE(ulPropTag, PT_BINARY)))) return S_OK;
			if (SUCCEEDED(mapi::DeleteProperty(m_lpProp, CHANGE_PROP_TYPE(ulPropTag, PT_UNICODE)))) return S_OK;
			if (SUCCEEDED(mapi::DeleteProperty(m_lpProp, CHANGE_PROP_TYPE(ulPropTag, PT_STRING8)))) return S_OK;
		}

		return hRes;
	};
} // namespace propertybag