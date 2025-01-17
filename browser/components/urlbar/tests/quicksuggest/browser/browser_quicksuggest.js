/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests browser quick suggestions.
 */

const TEST_URL = "http://example.com/quicksuggest";

const REMOTE_SETTINGS_RESULTS = [
  {
    id: 1,
    url: `${TEST_URL}?q=frabbits`,
    title: "frabbits",
    keywords: ["fra", "frab"],
    click_url: "http://click.reporting.test.com/",
    impression_url: "http://impression.reporting.test.com/",
    advertiser: "TestAdvertiser",
  },
  {
    id: 2,
    url: `${TEST_URL}?q=nonsponsored`,
    title: "Non-Sponsored",
    keywords: ["nonspon"],
    click_url: "http://click.reporting.test.com/nonsponsored",
    impression_url: "http://impression.reporting.test.com/nonsponsored",
    advertiser: "TestAdvertiserNonSponsored",
    iab_category: "5 - Education",
  },
];

add_setup(async function () {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await UrlbarTestUtils.formHistory.clear();

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsResults: [
      {
        type: "data",
        attachment: REMOTE_SETTINGS_RESULTS,
      },
    ],
  });
});

// Tests a sponsored result and keyword highlighting.
add_task(async function sponsored() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "fra",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: true,
    url: `${TEST_URL}?q=frabbits`,
  });
  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  Assert.equal(
    row.querySelector(".urlbarView-title").firstChild.textContent,
    "fra",
    "The part of the keyword that matches users input is not bold."
  );
  Assert.equal(
    row.querySelector(".urlbarView-title > strong").textContent,
    "b",
    "The auto completed section of the keyword is bolded."
  );
  await UrlbarTestUtils.promisePopupClose(window);
});

// Tests a non-sponsored result.
add_task(async function nonSponsored() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "nonspon",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: false,
    url: `${TEST_URL}?q=nonsponsored`,
  });
  await UrlbarTestUtils.promisePopupClose(window);
});

// Tests sponsored priority feature.
add_task(async function sponsoredPriority() {
  const cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestSponsoredPriority: true,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "fra",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: true,
    isBestMatch: true,
    isEmptyDescription: true,
    url: `${TEST_URL}?q=frabbits`,
  });

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  Assert.equal(
    row.querySelector(".urlbarView-title").firstChild.textContent,
    "fra",
    "The part of the keyword that matches users input is not bold."
  );
  Assert.equal(
    row.querySelector(".urlbarView-title > strong").textContent,
    "b",
    "The auto completed section of the keyword is bolded."
  );

  // Group label.
  let before = window.getComputedStyle(row, "::before");
  Assert.equal(before.content, "attr(label)", "::before.content is enabled");
  Assert.equal(
    row.getAttribute("label"),
    "Sponsored",
    "Row has 'Sponsored' group label"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await cleanUpNimbus();
});

// Tests sponsored priority feature does not affect to non-sponsored suggestion.
add_task(async function sponsoredPriorityButNotSponsoredSuggestion() {
  const cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestSponsoredPriority: true,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "nonspon",
  });
  await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    index: 1,
    isSponsored: false,
    url: `${TEST_URL}?q=nonsponsored`,
  });

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  let before = window.getComputedStyle(row, "::before");
  Assert.equal(before.content, "attr(label)", "::before.content is enabled");
  Assert.equal(
    row.getAttribute("label"),
    "Firefox Suggest",
    "Row has general group label for quick suggest"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await cleanUpNimbus();
});
