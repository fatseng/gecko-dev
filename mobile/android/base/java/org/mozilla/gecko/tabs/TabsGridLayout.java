/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.tabs;

import org.mozilla.gecko.R;
import org.mozilla.gecko.widget.GridSpacingDecoration;

import android.content.Context;
import android.content.res.Resources;
import android.support.v7.widget.GridLayoutManager;
import android.support.v7.widget.helper.ItemTouchHelper;
import android.util.AttributeSet;
import android.util.Log;

public class TabsGridLayout extends TabsLayout {
    private static final String LOGTAG = "Gecko" + TabsGridLayout.class.getSimpleName();

    private GridSpacingDecoration spacingDecoration;
    private final int desiredItemWidth;
    private final int desiredHorizontalItemSpacing;
    private final int minHorizontalItemSpacing;
    private final int verticalItemPadding;

    public TabsGridLayout(Context context, AttributeSet attrs) {
        super(context, attrs, R.layout.tabs_layout_item_view);

        final Resources resources = context.getResources();

        // Actual span count is updated in onSizeChanged.
        setLayoutManager(new GridLayoutManager(context, 1));

        desiredItemWidth = resources.getDimensionPixelSize(R.dimen.tab_panel_item_width);
        desiredHorizontalItemSpacing = resources.getDimensionPixelSize(R.dimen.tab_panel_grid_ideal_item_hspacing);
        minHorizontalItemSpacing = resources.getDimensionPixelOffset(R.dimen.tab_panel_grid_min_item_hspacing);
        verticalItemPadding = resources.getDimensionPixelSize(R.dimen.tab_panel_grid_item_vpadding);
        final int viewPaddingHorizontal = resources.getDimensionPixelSize(R.dimen.tab_panel_grid_hpadding);
        final int viewPaddingVertical = resources.getDimensionPixelSize(R.dimen.tab_panel_grid_vpadding);

        setPadding(viewPaddingHorizontal, viewPaddingVertical, viewPaddingHorizontal, viewPaddingVertical);
        setClipToPadding(false);
        setScrollBarStyle(SCROLLBARS_OUTSIDE_OVERLAY);

        setItemAnimator(new TabsGridLayoutAnimator());

        // A TouchHelper handler for swipe to close.
        final TabsTouchHelperCallback callback = new TabsTouchHelperCallback(this) {
            @Override
            protected float alphaForItemSwipeDx(float dX, int distanceToAlphaMin) {
                return 1f - 2f * Math.abs(dX) / distanceToAlphaMin;
            }
        };
        final ItemTouchHelper touchHelper = new ItemTouchHelper(callback);
        touchHelper.attachToRecyclerView(this);
    }

    @Override
    public void closeAll() {
        autoHidePanel();

        closeAllTabs();
    }

    @Override
    protected boolean addAtIndexRequiresScroll(int index) {
        final GridLayoutManager layoutManager = (GridLayoutManager) getLayoutManager();
        final int spanCount = layoutManager.getSpanCount();
        final int firstVisibleIndex = layoutManager.findFirstVisibleItemPosition();
        // When you add an item at the first visible position to a GridLayoutManager and there's
        // room to scroll, RecyclerView scrolls the new position to anywhere from near the bottom of
        // its row to completely offscreen (for unknown reasons), so we need to scroll to fix that.
        // We also scroll when the item being added is the only item on the final row.
        return index == firstVisibleIndex ||
                (index == getAdapter().getItemCount() - 1 && index % spanCount == 0);
    }

    private void updateSpacingDecoration(int horizontalItemSpacing) {
        if (spacingDecoration != null) {
            removeItemDecoration(spacingDecoration);
        }
        spacingDecoration = new GridSpacingDecoration(horizontalItemSpacing, verticalItemPadding);
        addItemDecoration(spacingDecoration);
        updateSelectedPosition();
    }

    @Override
    public void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);

        if (w == oldw) {
            return;
        }

        final GridLayoutManager layoutManager = (GridLayoutManager) getLayoutManager();

        final int nonPaddingWidth = w - getPaddingLeft() - getPaddingRight();
        // We lay out the tabs so that the outer two tab edges are butted up against the
        // RecyclerView padding, and then all other tab edges get their own padding, so
        // nonPaddingWidth in terms of tab width w and tab spacing s for n tabs is
        //   n * w + (n - 1) * s
        // Solving for n gives the formulas below.
        final int idealSpacingSpanCount = Math.max(1,
                (nonPaddingWidth + desiredHorizontalItemSpacing) / (desiredItemWidth + desiredHorizontalItemSpacing));
        final int maxSpanCount = Math.max(1,
                (nonPaddingWidth + minHorizontalItemSpacing) / (desiredItemWidth + minHorizontalItemSpacing));

        // General caution note: span count can change here at a point where some ItemDecorations
        // have been computed and some have not, and Android doesn't recompute ItemDecorations after
        // a setSpanCount call, so we need to always remove and then add back our spacingDecoration
        // (whose computations depend on spanCount) in order to get a full layout recompute.
        if (idealSpacingSpanCount == maxSpanCount) {
            layoutManager.setSpanCount(idealSpacingSpanCount);
            updateSpacingDecoration(desiredHorizontalItemSpacing);
        } else {
            // We're gaining a column by decreasing the item spacing - this actually turns out to be
            // necessary to fit three columns in landscape mode on many phones.  It also allows us
            // to match the span counts produced by the previous GridLayout implementation.
            layoutManager.setSpanCount(maxSpanCount);

            // Increase the spacing as much as we can without giving up our increased span count.
            for (int spacing = minHorizontalItemSpacing + 1; spacing <= desiredHorizontalItemSpacing; spacing++) {
                if (maxSpanCount * desiredItemWidth + (maxSpanCount - 1) * spacing > nonPaddingWidth) {
                    updateSpacingDecoration(spacing - 1);
                    return;
                }
            }
            // We should never get here if our calculations above were correct.
            Log.e(LOGTAG, "Span count calculation error");
            updateSpacingDecoration(minHorizontalItemSpacing);
        }
    }
}
