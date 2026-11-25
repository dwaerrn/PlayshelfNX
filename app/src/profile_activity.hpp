#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "app_data.hpp"

// "Choose a profile": one card per console profile with its picture, name and totals
class ProfileActivity : public brls::Activity {
  public:
    // onClose runs after the screen is gone; changed says whether another profile was chosen
    explicit ProfileActivity(std::function<void(bool changed)> onClose);

    brls::View* createContentView() override;

  private:
    std::vector<ProfileItem> items_;
    std::function<void(bool)> onClose_;
    bool closing_ = false;

    void close(bool changed);
};
