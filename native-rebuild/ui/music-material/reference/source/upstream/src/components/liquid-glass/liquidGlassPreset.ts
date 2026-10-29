export type LiquidGlassStudioPreset = {
    refThickness: number;
    refFactor: number;
    refDispersion: number;
    refFresnelRange: number;
    refFresnelHardness: number;
    refFresnelFactor: number;
    glareRange: number;
    glareHardness: number;
    glareFactor: number;
    glareConvergence: number;
    glareOppositeFactor: number;
    glareAngle: number;
    blurRadius: number;
    blurEdge: boolean;
    tint: readonly [number, number, number, number];
    shadowExpand: number;
    shadowFactor: number;
    shadowPosition: readonly [number, number];
    shapeRadius: number;
    shapeRoundness: number;
    surface: {
        width: number;
        height: number;
        radiusPercent: number;
    };
};

// Values exported by the user's Liquid Glass Studio snapshot.
export const LIQUID_GLASS_STUDIO_PRESET: LiquidGlassStudioPreset = {
    refThickness: 20,
    refFactor: 1.4,
    refDispersion: 14,
    refFresnelRange: 30,
    refFresnelHardness: 0.2,
    refFresnelFactor: 0.2,
    glareRange: 30,
    glareHardness: 0.2,
    glareFactor: 0.9,
    glareConvergence: 0.5,
    glareOppositeFactor: 0.8,
    glareAngle: -45,
    blurRadius: 1,
    blurEdge: true,
    tint: [1, 1, 1, 0],
    shadowExpand: 25,
    shadowFactor: 0.15,
    shadowPosition: [0, -10],
    shapeRadius: 1,
    shapeRoundness: 2,
    surface: {
        width: 600,
        height: 90,
        radiusPercent: 100,
    },
};
