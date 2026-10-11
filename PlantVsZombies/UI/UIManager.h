#pragma once
#ifndef _UIMANAGER_H
#define _UIMANAGER_H
#include "../ResourceManager.h"
#include "ButtonManager.h"
#include "SliderManager.h"
#include "GameMessageBox.h"
#include "../Graphics.h"
#include <algorithm>
#include <vector>

class UIManager
{
private:
	ButtonManager buttonManager;
	SliderManager sliderManager;
	std::vector<std::shared_ptr<GameMessageBox>> messageBoxes;

	// 控件回调可能请求关闭弹窗；统一等 Button/Slider 完成遍历后再解除注册。
	void FlushClosedMessageBoxes()
	{
		for (const auto& messageBox : messageBoxes) {
			if (messageBox && messageBox->IsCloseRequested()) {
				messageBox->DetachControls();
			}
		}
		std::erase_if(messageBoxes, [](const std::shared_ptr<GameMessageBox>& messageBox) {
			return !messageBox || messageBox->IsCloseRequested();
		});
	}

	/** 顶层模态框独占按钮/滑块输入；下层框保持绘制且保留原有控件状态。 */
	void RefreshModalInput()
	{
		const auto top = GetTopActiveMessageBox();
		for (size_t i = 0; i < buttonManager.GetButtonCount(); ++i) {
			const auto button = buttonManager.GetButton(i);
			button->SetModalInputBlocked(top && std::find(top->m_buttons.begin(),
				top->m_buttons.end(), button) == top->m_buttons.end());
		}
		for (size_t i = 0; i < sliderManager.GetSliderCount(); ++i) {
			const auto slider = sliderManager.GetSlider(i);
			slider->SetModalInputBlocked(top && std::find(top->m_sliders.begin(),
				top->m_sliders.end(), slider) == top->m_sliders.end());
		}
	}

public:
	~UIManager()
	{
		ClearAll();
	}

	std::shared_ptr<Button> CreateButton(Vector pos = Vector::zero(), Vector size = Vector(40, 40))
	{
		return buttonManager.CreateButton(pos, size);
	}

	void AddMessageBox(const std::shared_ptr<GameMessageBox>& messageBox)
	{
		if (messageBox) messageBoxes.push_back(messageBox);
	}

	/** 返回当前最上层的活动模态框；没有时返回 nullptr。 */
	std::shared_ptr<GameMessageBox> GetTopActiveMessageBox() const
	{
		for (auto it = messageBoxes.rbegin(); it != messageBoxes.rend(); ++it) {
			if (*it && (*it)->IsActive()) return *it;
		}
		return nullptr;
	}

	/** 活动模态层数，供导航回归验证父子框生命周期。 */
	size_t GetActiveMessageBoxCount() const {
		return std::count_if(messageBoxes.begin(), messageBoxes.end(),
			[](const auto& box) { return box && box->IsActive(); });
	}

	void RemoveButton(std::shared_ptr<Button> button)
	{
		buttonManager.RemoveButton(button);
	}

	void ClearAllButtons()
	{
		buttonManager.ClearAllButtons();
	}

	size_t GetButtonCount() const
	{
		return buttonManager.GetButtonCount();
	}

	std::shared_ptr<Button> GetButton(size_t index) const
	{
		return buttonManager.GetButton(index);
	}

	std::shared_ptr<Slider> CreateSlider(Vector pos = Vector::zero(),
		Vector size = Vector(135, 10),
		float minVal = 0.0f,
		float maxVal = 1.0f,
		float initialValue = 0.5f)
	{
		return sliderManager.CreateSlider(pos, size, minVal, maxVal, initialValue);
	}

	void RemoveSlider(std::shared_ptr<Slider> slider)
	{
		sliderManager.RemoveSlider(slider);
	}

	void ClearAllSliders()
	{
		sliderManager.ClearAllSliders();
	}

	size_t GetSliderCount() const
	{
		return sliderManager.GetSliderCount();
	}

	std::shared_ptr<Slider> GetSlider(size_t index) const
	{
		return sliderManager.GetSlider(index);
	}

	/** 在读取鼠标边沿前按当前模态顶层设置输入门禁。 */
	void ProcessMouseEvent(InputHandler* input)
	{
		RefreshModalInput();
		buttonManager.ProcessMouseEvent(input);
		sliderManager.ProcessMouseEvent(input);
	}

	/** 更新允许输入的控件；回调创建子框后刷新滑块门禁，最后清理关闭层。 */
	void UpdateAll(InputHandler* input)
	{
		RefreshModalInput();
		buttonManager.UpdateAll(input);
		RefreshModalInput();
		sliderManager.UpdateAll(input);
		FlushClosedMessageBoxes();
	}

	/** 绘制普通控件；场景自定义绘制结束后再单独提交模态框。 */
	void DrawControls(Graphics* g) const
	{
		buttonManager.DrawAll(g);
		sliderManager.DrawAll(g);
	}

	/** 按创建顺序绘制活动框及自有控件，必须位于场景所有普通绘制命令之后。 */
	void DrawMessageBoxes(Graphics* g) const
	{
		for (const auto& messageBox : messageBoxes) {
			if (messageBox && messageBox->IsActive()) messageBox->Draw(g);
		}
	}

	void ResetAllFrameStates()
	{
		buttonManager.ResetAllFrameStates();
	}

	void ClearAll()
	{
		for (const auto& messageBox : messageBoxes) {
			if (messageBox) messageBox->DetachControls();
		}
		messageBoxes.clear();
		buttonManager.ClearAllButtons();
		sliderManager.ClearAllSliders();
	}

	ButtonManager& GetButtonManager() { return buttonManager; }
	SliderManager& GetSliderManager() { return sliderManager; }
	const ButtonManager& GetButtonManager() const { return buttonManager; }
	const SliderManager& GetSliderManager() const { return sliderManager; }
};

#endif
