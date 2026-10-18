#pragma once
#include <memory>

#include "../rpg/scene.h"
#include "window_selectable.h"

namespace ui {

class SceneMenuBase : public rpg::Scene {
public:
    void create() override;
    rpg::GameActor* actor() const { return actor_; }
    void update_actor();
    virtual void create_background();
    void set_background_opacity(double opacity);
    virtual void create_help_window();
    void next_actor();
    void previous_actor();
    virtual void on_actor_change() {}
    void check_gameover();
    void terminate() override;

    std::shared_ptr<g2d::Sprite> background_sprite;
    std::shared_ptr<WindowHelp> help_window;

protected:
    rpg::GameActor* actor_ = nullptr;
};

}
