/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that the settings component is rendered as expected when
 * `browser.shopping.experience2023.ads.enabled` is true.
 */
add_task(async function test_shopping_settings_ads_enabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.shopping.experience2023.ads.enabled", true]],
  });

  await BrowserTestUtils.withNewTab(
    {
      url: "about:shoppingsidebar",
      gBrowser,
    },
    async browser => {
      let shoppingSettings = await getSettingsDetails(
        browser,
        MOCK_POPULATED_DATA
      );
      ok(shoppingSettings.settingsEl, "Got the shopping-settings element");

      let toggle = shoppingSettings.recommendationsToggleEl;
      ok(toggle, "There should be a toggle");

      let optOutButton = shoppingSettings.optOutButtonEl;
      ok(optOutButton, "There should be an opt-out button");
    }
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the settings component is rendered as expected when
 * `browser.shopping.experience2023.ads.enabled` is false.
 */
add_task(async function test_shopping_settings_ads_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.shopping.experience2023.ads.enabled", false]],
  });

  await BrowserTestUtils.withNewTab(
    {
      url: "about:shoppingsidebar",
      gBrowser,
    },
    async browser => {
      let shoppingSettings = await getSettingsDetails(
        browser,
        MOCK_POPULATED_DATA
      );
      ok(shoppingSettings.settingsEl, "Got the shopping-settings element");

      let toggle = shoppingSettings.recommendationsToggleEl;
      ok(!toggle, "There should be no toggle");

      let optOutButton = shoppingSettings.optOutButtonEl;
      ok(optOutButton, "There should be an opt-out button");
    }
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the shopping-settings ads toggle and ad render correctly, even with
 * multiple tabs. If `browser.shopping.experience2023.ads.userEnabled`
 * is false in one tab, it should be false for all other tabs with the shopping sidebar open.
 *
 * Temporarily disabled; see bug 1851891 for details.
 */
add_task(async function test_settings_toggle_ad_and_multiple_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.shopping.experience2023.ads.enabled", true],
      ["browser.shopping.experience2023.ads.userEnabled", true],
    ],
  });

  // Tab 1 - ad is visible at first and then toggle is selected to set ads.userEnabled to false.
  let tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:shoppingsidebar"
  );
  let browser1 = tab1.linkedBrowser;

  let mockArgs = {
    mockData: MOCK_ANALYZED_PRODUCT_RESPONSE,
    mockRecommendationData: MOCK_RECOMMENDED_ADS_RESPONSE,
  };
  await SpecialPowers.spawn(browser1, [mockArgs], async args => {
    const { mockData, mockRecommendationData } = args;
    let shoppingContainer =
      content.document.querySelector("shopping-container").wrappedJSObject;

    let adVisiblePromise = ContentTaskUtils.waitForCondition(() => {
      return (
        shoppingContainer.recommendedAdEl &&
        ContentTaskUtils.is_visible(shoppingContainer.recommendedAdEl)
      );
    }, "Waiting for recommended-ad to be visible");

    shoppingContainer.data = Cu.cloneInto(mockData, content);
    shoppingContainer.recommendationData = Cu.cloneInto(
      mockRecommendationData,
      content
    );
    // TODO: Until we have proper mocks of data passed from ShoppingSidebarChild,
    // hardcode `adsEnabled` and `adsEnabledByUser` so that we can test ad visibility.
    shoppingContainer.adsEnabled = true;
    shoppingContainer.adsEnabledByUser = true;

    await shoppingContainer.updateComplete;
    await adVisiblePromise;

    let shoppingSettings = shoppingContainer.settingsEl;
    ok(shoppingSettings, "Got the shopping-settings element");

    let toggle = shoppingSettings.recommendationsToggleEl;
    ok(toggle, "There should be a toggle");
    ok(toggle.hasAttribute("pressed"), "Toggle should have enabled state");

    ok(
      SpecialPowers.getBoolPref(
        "browser.shopping.experience2023.ads.userEnabled"
      ),
      "ads userEnabled pref should be true"
    );

    let adRemovedPromise = ContentTaskUtils.waitForCondition(() => {
      return !shoppingContainer.recommendedAdEl;
    }, "Waiting for recommended-ad to be removed");

    toggle.click();

    await adRemovedPromise;

    ok(!toggle.hasAttribute("pressed"), "Toggle should have disabled state");
    ok(
      !SpecialPowers.getBoolPref(
        "browser.shopping.experience2023.ads.userEnabled"
      ),
      "ads userEnabled pref should be false"
    );
  });

  // Tab 2 - ads.userEnabled should still be false and ad should not be visible.
  let tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:shoppingsidebar"
  );
  let browser2 = tab2.linkedBrowser;

  await SpecialPowers.spawn(browser2, [mockArgs], async args => {
    const { mockData, mockRecommendationData } = args;
    let shoppingContainer =
      content.document.querySelector("shopping-container").wrappedJSObject;

    shoppingContainer.data = Cu.cloneInto(mockData, content);
    shoppingContainer.recommendationData = Cu.cloneInto(
      mockRecommendationData,
      content
    );
    // TODO: Until we have proper mocks of data passed from ShoppingSidebarChild,
    // hardcode `adsEnabled` so that we can test ad visibility.
    shoppingContainer.adsEnabled = true;

    await shoppingContainer.updateComplete;

    ok(
      !shoppingContainer.recommendedAdEl,
      "There should be no ads in the new tab"
    );
    ok(
      !SpecialPowers.getBoolPref(
        "browser.shopping.experience2023.ads.userEnabled"
      ),
      "ads userEnabled pref should be false"
    );
  });

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);

  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
}).skip();
