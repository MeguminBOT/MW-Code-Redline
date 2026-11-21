// complete
// bases: public FEStatWidget
FEToggleWidget(bool enabled);
virtual ~FEToggleWidget();
void CheckMouse(const char *name, float x, float y) override;
void Position() override;
void Show() override;
void Hide() override;
void Enable() override;
void Disable() override;
void SetFocus(const char *name) override;
void UnsetFocus() override;
virtual void BlinkArrows(unsigned int action);
void SetScript(unsigned int script);
bool Update(unsigned int action);
class FEImage *pLeftImage;
class FEImage *pRightImage;
unsigned int EnableScript;
unsigned int DisableScript;
