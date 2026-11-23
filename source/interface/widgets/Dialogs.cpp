/**
 * Showing a dialog (original: zFe2 DialogInterface::ShowDialog 0x5983c0). A feDialogConfig (0x240 bytes, +0x200 its
 * kind: 4 is an error, pushed above everything; +0x228 the package) is copied to SecretDialogInfo and the package
 * pushed with it; the package name picks the layout ("animating", "3button", "3buttons_stacked", or the in-game one
 * during a race).
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const char *const kAnimating = reinterpret_cast<const char *>(0x89b340);
const char *const kThreeButton = reinterpret_cast<const char *>(0x89b32c);
const char *const kThreeStacked = reinterpret_cast<const char *>(0x89ec24);
const char *const kOnlineDialog = reinterpret_cast<const char *>(0x89f7b4);
const char *const kStackedDialog = reinterpret_cast<const char *>(0x89f794);
const char *const kInGameDialog = reinterpret_cast<const char *>(0x89736c);
const char *const kDialog = reinterpret_cast<const char *>(0x89e580);
char *const kSecretDialogInfo = reinterpret_cast<char *>(0x8f5128);
int *const kCurrDialogNum = reinterpret_cast<int *>(0x8f5360);
int *const kDialogHandle = reinterpret_cast<int *>(0x8f42bc);
const int *const kGameFlowState = reinterpret_cast<const int *>(0x925e90);
char *const *const kFEDatabase = reinterpret_cast<char *const *>(0x91cf90);
const int kConfigSize = 0x240;
const int kPackage = 0x228;
const int kKind = 0x200;
const int kErrorKind = 4;
} // namespace

int DialogInterface::ShowDialog(feDialogConfig *config)
{
    const char *&package = At<const char *>(config, kPackage);
    const char *name = package;
    if (!name || !*name)
        package = *kGameFlowState == 6 ? kInGameDialog : kDialog;
    else if (!bStrCmp(name, kAnimating))
        package = kOnlineDialog;
    else if (!bStrCmp(name, kThreeButton))
        package = kDialog;
    else if (!bStrCmp(name, kThreeStacked))
        package = kStackedDialog;
    if (*kCurrDialogNum)
        DismissDialog(*kCurrDialogNum);
    const unsigned int *from = reinterpret_cast<const unsigned int *>(config);
    unsigned int *to = reinterpret_cast<unsigned int *>(kSecretDialogInfo);
    for (int i = 0; i < kConfigSize / 4; ++i)
        to[i] = from[i];
    *kCurrDialogNum = *kDialogHandle;
    ++*kDialogHandle;
    const char *shown = At<const char *>(kSecretDialogInfo, kPackage);
    FEngSetCreateCallback(shown, DialogCreater);
    if (At<int>(kSecretDialogInfo, kKind) == kErrorKind)
    {
        unsigned int mask;
        switch (At<signed char>(*kFEDatabase, 0xc))
        {
        case 0:
            mask = 1;
            break;
        case 1:
            mask = 2;
            break;
        case 2:
            mask = 4;
            break;
        case 3:
            mask = 8;
            break;
        default:
            mask = 0;
            break;
        }
        cFEng::mInstance->PushErrorPackage(shown, reinterpret_cast<int>(kSecretDialogInfo), mask);
        return *kCurrDialogNum;
    }
    cFEng::mInstance->QueuePackagePush(shown, reinterpret_cast<int>(kSecretDialogInfo), 0, false);
    return *kCurrDialogNum;
}
