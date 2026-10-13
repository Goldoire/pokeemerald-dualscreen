package com.pokeemerald.experimental;

import android.app.Presentation;
import android.content.Context;
import android.os.Bundle;
import android.view.Display;
import android.view.ViewGroup;

/** Hosts the DualScreenView on a secondary (bottom) display. */
public final class DualScreenPresentation extends Presentation {
    private DualScreenView view;
    private Runnable settingsListener;
    private Runnable changeSavePathListener;

    public void setChangeSavePathListener(Runnable listener) {
        changeSavePathListener = listener;
        if (view != null) view.setChangeSavePathListener(listener);
    }

    public DualScreenPresentation(Context context, Display display) {
        super(context, display);
    }

    public void setSettingsListener(Runnable listener) {
        settingsListener = listener;
        if (view != null) {
            view.setSettingsListener(listener);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Never take key focus: the game activity must keep receiving
        // controller input while the bottom screen is touched.
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        view = new DualScreenView(getContext());
        view.setChangeSavePathListener(changeSavePathListener);
        view.setSettingsListener(settingsListener);
        setContentView(view, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    public void updateState(DualScreenState state) {
        if (view != null) {
            view.setState(state);
        }
    }

    /** Cycles the bottom-screen tabs when extra controller input is enabled. */
    public void switchTab(int step) {
        if (view != null) view.switchControllerTab(step);
    }

    public boolean extraControllerAvailable() {
        return view != null && view.extraControllerAvailable();
    }

    public void navigateExtra(int action) {
        if (view != null) view.navigateExtra(action);
    }

    /** Routes the existing battle-navigation controls to the bottom screen. */
    public void navigate(int action) {
        if (view != null) {
            view.navigate(action);
        }
    }
}
