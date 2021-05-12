package com.plushnode.nullspace;

import android.app.NativeActivity;
import android.content.Context;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.inputmethod.InputMethodManager;

import java.util.concurrent.LinkedBlockingQueue;

public class MainActivity extends NativeActivity {
    LinkedBlockingQueue<Integer> unicodeCharacterQueue = new LinkedBlockingQueue<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    public void showSoftInput() {
        InputMethodManager inputMethodManager = (InputMethodManager)this.getSystemService(Context.INPUT_METHOD_SERVICE);

        inputMethodManager.showSoftInput(this.getWindow().getDecorView(), 0);
    }

    public void hideSoftInput() {
        InputMethodManager inputMethodManager = (InputMethodManager)this.getSystemService(Context.INPUT_METHOD_SERVICE);

        inputMethodManager.hideSoftInputFromWindow(this.getWindow().getDecorView().getWindowToken(), 0);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            unicodeCharacterQueue.offer(event.getUnicodeChar(event.getMetaState()));
        }

        return super.dispatchKeyEvent(event);
    }

    public int pollUnicodeChar() {
        Integer result = unicodeCharacterQueue.poll();

        return result == null ? 0 : result;
    }
}