/**
 * Message filter of EA's memory card library (original: RealmcIface::MemcardInterfaceImpl::_ProcessGuidelinesMessage
 * 0x7f4b97). A hidden message is not shown; the library answers it with its default choice and carries on.
 *
 * Change from the original: while the profile's autosave is on, "Save complete." and "Load complete." are hidden too,
 * so the profile load at boot and every save finish without stopping on a Continue prompt.
 */
#include <stddef.h>

#include "game/realmemcard/Realmc.h"

namespace
{
struct GuidelineFilter
{
    unsigned int Id;
    RealmcIface::MessageChoices DefaultChoice;
};

/** SetMessage bit and default answer of a guideline message; Id 0 means it is always shown. */
GuidelineFilter FilterFor(unsigned int message)
{
    using namespace RealmcIface;
    switch (message)
    {
    case Realmc::TRC_SAVE_READY:
        return {ID_SAVE_READY, CHOICE_OPTION2};
    case Realmc::TRC_SAVE_COMPLETE:
        return {ID_SAVE_COMPLETE, CHOICE_OPTION1};
    case Realmc::TRC_SAVE_FAILED:
        return {ID_SAVE_FAILED, CHOICE_OPTION1};
    case Realmc::TRC_LOAD_COMPLETE:
        return {ID_LOAD_COMPLETE, CHOICE_OPTION1};
    case Realmc::TRC_LOAD_FAILED:
        return {ID_LOAD_FAILED, CHOICE_OPTION1};
    case Realmc::TRC_DELETE_READY:
        return {ID_DELETE_READY, CHOICE_OPTION2};
    case Realmc::TRC_DELETE_COMPLETE:
        return {ID_DELETE_COMPLETE, CHOICE_OPTION1};
    case Realmc::TRC_DELETE_FAILED:
        return {ID_DELETE_FAILED, CHOICE_OPTION1};
    default:
        return {0, CHOICE_NONE};
    }
}

/** Messages that only confirm success: skipped while autosave is on. */
unsigned int AutoSaveHiddenMessages()
{
    if (FEDatabase == nullptr || FEDatabase->CurrentUserProfile == nullptr || !FEDatabase->IsAutoSave())
        return 0;
    return RealmcIface::ID_SAVE_COMPLETE | RealmcIface::ID_LOAD_COMPLETE;
}
} // namespace

static_assert(offsetof(RealmcIface::MemcardInterfaceImpl, mTaskManager) == 0x18, "MemcardInterfaceImpl layout");
static_assert(offsetof(RealmcIface::MemcardInterfaceImpl, mAutosaveEnabled) == 0x6A, "MemcardInterfaceImpl layout");
static_assert(offsetof(RealmcIface::MemcardInterfaceImpl, mUserChoice) == 0x74, "MemcardInterfaceImpl layout");
static_assert(offsetof(RealmcIface::MemcardInterfaceImpl, mHiddenMessages) == 0x7C, "MemcardInterfaceImpl layout");
static_assert(offsetof(RealmcIface::MemcardInterfaceImpl, mForceMessages) == 0x80, "MemcardInterfaceImpl layout");
static_assert(offsetof(Realmc::Message, trc) == 0xC, "Realmc::Message layout");
static_assert(offsetof(Realmc::Trc, mOptions) == 0xC, "Realmc::Trc layout");

void RealmcIface::MemcardInterfaceImpl::_ProcessGuidelinesMessage(Realmc::Message *message)
{
    unsigned int hidden = mHiddenMessages;
    if (mAutosaveEnabled)
        hidden |= ID_SAVE_READY | ID_SAVE_COMPLETE;
    hidden |= mTaskManager->FilterGuidelinesMessage(message);
    hidden |= AutoSaveHiddenMessages();
    if (mForceMessages != 0)
        hidden &= ~mForceMessages;

    const GuidelineFilter filter = FilterFor(message->trc.mMsgId);
    if (filter.Id == 0 || (hidden & filter.Id) == 0)
        _ShowGuidelinesMessage(&message->trc);
    else
        mUserChoice = filter.DefaultChoice;
}
