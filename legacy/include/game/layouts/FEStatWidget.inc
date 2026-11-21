// complete
// bases: public FEWidget
FEStatWidget(bool enabled);
virtual ~FEStatWidget();
void Act(const char *name, unsigned int action) override;
void CheckMouse(const char *name, float x, float y) override;
void Position() override;
void Show() override;
void Hide() override;
void SetFocus(const char *name) override;
void UnsetFocus() override;
void SetPos(bVector2 &pos) override;
void SetPosX(float x) override;
void SetPosY(float y) override;
class FEString *GetDataObject();
class FEString *GetTitleObject();
class FEString *pTitle;
class FEString *pData;
bVector2 vMaxTitleSize;
bVector2 vMaxDataSize;
bVector2 vDataPos;
