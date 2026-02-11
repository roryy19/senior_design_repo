import React, { createContext, useContext, useState, useEffect, ReactNode } from 'react';
import { loadFontScale, saveFontScale } from '../storage/registry';

type FontSizeContextType = {
  fontScale: number;
  setFontScale: (scale: number) => void;
};

const FontSizeContext = createContext<FontSizeContextType>({
  fontScale: 1,
  setFontScale: () => {},
});

type FontSizeProviderProps = {
  children: ReactNode;
};

export function FontSizeProvider({ children }: FontSizeProviderProps) {
  const [fontScale, setFontScaleState] = useState(1);

  useEffect(() => {
    loadFontScale().then(setFontScaleState);
  }, []);

  const setFontScale = (scale: number) => {
    setFontScaleState(scale);
    saveFontScale(scale);
  };

  return (
    <FontSizeContext.Provider value={{ fontScale, setFontScale }}>
      {children}
    </FontSizeContext.Provider>
  );
}

export const useFontSize = () => useContext(FontSizeContext);
