// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/font_render_params.h"

#include <limits>

#include "base/check_op.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_reg_util_win.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ui_base_features.h"
#include "ui/gfx/font_util_win.h"

namespace gfx {

class FontRenderParamsTest : public testing::Test {
 public:
  FontRenderParamsTest() { ClearFontRenderParamsCacheForTest(); }

  FontRenderParamsTest(const FontRenderParamsTest&) = delete;
  FontRenderParamsTest& operator=(const FontRenderParamsTest&) = delete;

  ~FontRenderParamsTest() override {}

 protected:
  registry_util::RegistryOverrideManager registry_override_manager_;
};

namespace {
// The ranges returned from `IDWriteRenderingParams` differ from the
// registry settings - contrast has a 100x multiplier and gamma has a 1000x
// multiplier.
constexpr float kContrastMultiplier = 100;
constexpr float kGammaMultiplier = 1000;
}  // namespace

TEST_F(FontRenderParamsTest, SystemFontSettingsDisabled) {
  // Ensure that without the feature enabled, the values of `FontRenderParams`
  // match Skia default values.
  FontRenderParams params =
      GetFontRenderParams(FontRenderParamsQuery(), nullptr);
  EXPECT_EQ(params.text_contrast, SK_GAMMA_CONTRAST);
  EXPECT_EQ(params.text_gamma, SK_GAMMA_EXPONENT);
}

TEST_F(FontRenderParamsTest, DefaultRegistryState) {
  // Ensure that with the feature enabled, the values of `FontRenderParams`
  // match the associated registry key values.
  base::test::ScopedFeatureList scoped_features(
      features::kUseGammaContrastRegistrySettings);

  FontRenderParams params =
      GetFontRenderParams(FontRenderParamsQuery(), nullptr);

  base::win::RegKey key = FontUtilWin::GetTextSettingsRegistryKey();
  if (key.Valid()) {
    DWORD contrast;
    ASSERT_EQ(key.ReadValueDW(L"EnhancedContrastLevel", &contrast),
              ERROR_SUCCESS);
    DWORD gamma;
    ASSERT_EQ(key.ReadValueDW(L"GammaLevel", &gamma), ERROR_SUCCESS);

    EXPECT_FLOAT_EQ(params.text_contrast * kContrastMultiplier, contrast);
    EXPECT_FLOAT_EQ(params.text_gamma * kGammaMultiplier, gamma);
  } else {
    // If the registry keys aren't set, `IDWriteRenderingParams` defaults
    // to the following hard-coded values:
    constexpr float default_contrast = 0.5f;
    constexpr float default_gamma = 1.8f;
    EXPECT_FLOAT_EQ(params.text_contrast, default_contrast);
    EXPECT_FLOAT_EQ(params.text_gamma, default_gamma);
  }
}

TEST_F(FontRenderParamsTest, OverrideRegistryValues) {
  // Ensure that the values returned from `GetContrastFromRegistry` and
  // `GetGammaFromRegistry` reflect the state of the registry when
  // `kUseGammaContrastRegistrySettings` is enabled.
  base::test::ScopedFeatureList scoped_features(
      features::kUseGammaContrastRegistrySettings);

  // Override the registry to maintain test machine state.
  ASSERT_NO_FATAL_FAILURE(
      registry_override_manager_.OverrideRegistry(HKEY_CURRENT_USER));

  base::win::RegKey key = FontUtilWin::GetTextSettingsRegistryKey(KEY_WRITE);

  if (key.Valid()) {
    // Write non-default values for contrast and gamma.
    DWORD contrast = 75;
    ASSERT_EQ(key.WriteValue(L"EnhancedContrastLevel", contrast),
              ERROR_SUCCESS);
    DWORD gamma = 1900;
    ASSERT_EQ(key.WriteValue(L"GammaLevel", gamma), ERROR_SUCCESS);
    key.Close();

    // Verify that the contrast and gamma getters return non-defaults above.
    EXPECT_FLOAT_EQ(
        FontUtilWin::GetContrastFromRegistry() * kContrastMultiplier, contrast);
    EXPECT_FLOAT_EQ(FontUtilWin::GetGammaFromRegistry() * kGammaMultiplier,
                    gamma);
  }
}

}  // namespace gfx
