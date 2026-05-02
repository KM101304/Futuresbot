#include "sierrachart.h"

SCDLLName("Advanced Order Flow Trap Analyzer")

struct ClusterStats
{
    double VWAP = 0.0;
    double POC = 0.0;
    double Midpoint = 0.0;
    double Skew = 0.0;
    double TotalVAPVolume = 0.0;
    bool HasVAP = false;
};

static double SafeDiv(double numerator, double denominator)
{
    if (denominator == 0.0)
        return 0.0;
    return numerator / denominator;
}

static double ManualATR(SCStudyInterfaceRef sc, int index, int length)
{
    if (index <= 0 || length <= 0)
        return 0.0;

    const int start = max(1, index - length + 1);
    double sumTR = 0.0;
    int count = 0;

    for (int i = start; i <= index; ++i)
    {
        const double highLow = sc.High[i] - sc.Low[i];
        const double highPrevClose = fabs(sc.High[i] - sc.Close[i - 1]);
        const double lowPrevClose = fabs(sc.Low[i] - sc.Close[i - 1]);
        const double trueRange = max(highLow, max(highPrevClose, lowPrevClose));
        sumTR += trueRange;
        ++count;
    }

    return count > 0 ? sumTR / count : 0.0;
}

static double SimpleAverage(SCFloatArrayRef values, int index, int length)
{
    if (length <= 0)
        return 0.0;

    const int start = max(0, index - length + 1);
    double sum = 0.0;
    int count = 0;

    for (int i = start; i <= index; ++i)
    {
        sum += values[i];
        ++count;
    }

    return count > 0 ? sum / count : 0.0;
}

static ClusterStats CalculateClusterStats(SCStudyInterfaceRef sc, int index)
{
    ClusterStats stats;
    stats.Midpoint = (sc.High[index] + sc.Low[index]) * 0.5;

    const double fallback = sc.Close[index];
    stats.VWAP = fallback;
    stats.POC = fallback;
    stats.Skew = stats.VWAP - stats.Midpoint;

    if (sc.VolumeAtPriceForBars == nullptr)
        return stats;

    const int numLevels = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(index);
    if (numLevels <= 0)
        return stats;

    double sumPriceTimesVolume = 0.0;
    double totalVolume = 0.0;
    unsigned int maxVolumeAtPrice = 0;
    double pocPrice = fallback;

    const s_VolumeAtPriceV2* vap = nullptr;

    for (int level = 0; level < numLevels; ++level)
    {
        if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(index, level, &vap) || vap == nullptr)
            continue;

        const double price = static_cast<double>(vap->PriceInTicks) * sc.TickSize;
        const unsigned int volume = vap->Volume;

        if (volume == 0)
            continue;

        sumPriceTimesVolume += price * static_cast<double>(volume);
        totalVolume += static_cast<double>(volume);

        if (volume > maxVolumeAtPrice)
        {
            maxVolumeAtPrice = volume;
            pocPrice = price;
        }
    }

    if (totalVolume > 0.0)
    {
        stats.HasVAP = true;
        stats.TotalVAPVolume = totalVolume;
        stats.VWAP = sumPriceTimesVolume / totalVolume;
        stats.POC = pocPrice;
        stats.Skew = stats.VWAP - stats.Midpoint;
    }

    return stats;
}

SCSFExport scsf_AdvancedOrderFlowTrapAnalyzer(SCStudyInterfaceRef sc)
{
    SCSubgraphRef ClusterVWAP = sc.Subgraph[0];
    SCSubgraphRef ClusterPOC = sc.Subgraph[1];
    SCSubgraphRef ClusterMidpoint = sc.Subgraph[2];
    SCSubgraphRef ClusterSkew = sc.Subgraph[3];
    SCSubgraphRef Delta = sc.Subgraph[4];
    SCSubgraphRef DeltaPct = sc.Subgraph[5];
    SCSubgraphRef ExtremeDeltaMarker = sc.Subgraph[6];
    SCSubgraphRef Efficiency = sc.Subgraph[7];
    SCSubgraphRef ATR = sc.Subgraph[8];
    SCSubgraphRef ATRMA = sc.Subgraph[9];
    SCSubgraphRef VolumePerTrade = sc.Subgraph[10];
    SCSubgraphRef LongTrapSignal = sc.Subgraph[11];
    SCSubgraphRef ShortTrapSignal = sc.Subgraph[12];
    SCSubgraphRef AbsorptionSignal = sc.Subgraph[13];
    SCSubgraphRef EfficiencyColor = sc.Subgraph[14];

    SCInputRef EnableCluster = sc.Input[0];
    SCInputRef EnableDelta = sc.Input[1];
    SCInputRef EnableEfficiency = sc.Input[2];
    SCInputRef EnableTickVolume = sc.Input[3];
    SCInputRef EnableATR = sc.Input[4];
    SCInputRef EnableTraps = sc.Input[5];
    SCInputRef EnableAbsorption = sc.Input[6];
    SCInputRef EnableDebugLogging = sc.Input[7];
    SCInputRef DeltaTrapThreshold = sc.Input[8];
    SCInputRef ExtremeDeltaThreshold = sc.Input[9];
    SCInputRef MinimumVolumeForDeltaSignal = sc.Input[10];
    SCInputRef ATRLength = sc.Input[11];
    SCInputRef ATRMALength = sc.Input[12];
    SCInputRef LowFollowThroughATRFactor = sc.Input[13];
    SCInputRef AbsorptionVolumeThreshold = sc.Input[14];
    SCInputRef AbsorptionMinMovementTicks = sc.Input[15];
    SCInputRef AbsorptionUseATR = sc.Input[16];
    SCInputRef ShowVWAPLine = sc.Input[17];
    SCInputRef ShowPOCLine = sc.Input[18];
    SCInputRef ShowTrapLabels = sc.Input[19];
    SCInputRef ShowAbsorptionHighlights = sc.Input[20];
    SCInputRef ShowEfficiencyHeatmap = sc.Input[21];
    SCInputRef ShowExtremeDeltaMarkers = sc.Input[22];
    SCInputRef RequireReversalConfirmation = sc.Input[23];
    SCInputRef ReversalLookaheadBars = sc.Input[24];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Advanced Order Flow Trap Analyzer";
        sc.StudyDescription = "Order-flow study combining VAP VWAP/POC, delta, efficiency, ATR context, trap logic, and absorption.";
        sc.AutoLoop = 0;
        sc.GraphRegion = 0;
        sc.ValueFormat = VALUEFORMAT_INHERITED;
        sc.MaintainVolumeAtPriceData = 1;
        sc.UpdateAlways = 0;

        ClusterVWAP.Name = "Cluster VWAP";
        ClusterVWAP.DrawStyle = DRAWSTYLE_LINE;
        ClusterVWAP.PrimaryColor = RGB(0, 180, 255);
        ClusterVWAP.LineWidth = 2;
        ClusterVWAP.DrawZeros = false;

        ClusterPOC.Name = "Cluster POC";
        ClusterPOC.DrawStyle = DRAWSTYLE_DASH;
        ClusterPOC.PrimaryColor = RGB(255, 200, 0);
        ClusterPOC.LineWidth = 2;
        ClusterPOC.DrawZeros = false;

        ClusterMidpoint.Name = "Cluster Midpoint";
        ClusterMidpoint.DrawStyle = DRAWSTYLE_IGNORE;
        ClusterMidpoint.DrawZeros = false;

        ClusterSkew.Name = "Cluster Skew";
        ClusterSkew.DrawStyle = DRAWSTYLE_IGNORE;
        ClusterSkew.DrawZeros = false;

        Delta.Name = "Delta";
        Delta.DrawStyle = DRAWSTYLE_IGNORE;
        Delta.DrawZeros = true;

        DeltaPct.Name = "Delta %";
        DeltaPct.DrawStyle = DRAWSTYLE_IGNORE;
        DeltaPct.DrawZeros = true;

        ExtremeDeltaMarker.Name = "Extreme Delta";
        ExtremeDeltaMarker.DrawStyle = DRAWSTYLE_POINT_ON_HIGH;
        ExtremeDeltaMarker.PrimaryColor = RGB(255, 255, 255);
        ExtremeDeltaMarker.LineWidth = 4;
        ExtremeDeltaMarker.DrawZeros = false;

        Efficiency.Name = "Price Efficiency";
        Efficiency.DrawStyle = DRAWSTYLE_IGNORE;
        Efficiency.DrawZeros = true;

        ATR.Name = "ATR";
        ATR.DrawStyle = DRAWSTYLE_IGNORE;
        ATR.DrawZeros = false;

        ATRMA.Name = "ATR MA";
        ATRMA.DrawStyle = DRAWSTYLE_IGNORE;
        ATRMA.DrawZeros = false;

        VolumePerTrade.Name = "Volume Per Trade";
        VolumePerTrade.DrawStyle = DRAWSTYLE_IGNORE;
        VolumePerTrade.DrawZeros = true;

        LongTrapSignal.Name = "TRAP LONG";
        LongTrapSignal.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        LongTrapSignal.PrimaryColor = RGB(255, 80, 80);
        LongTrapSignal.LineWidth = 4;
        LongTrapSignal.DrawZeros = false;

        ShortTrapSignal.Name = "TRAP SHORT";
        ShortTrapSignal.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        ShortTrapSignal.PrimaryColor = RGB(80, 255, 120);
        ShortTrapSignal.LineWidth = 4;
        ShortTrapSignal.DrawZeros = false;

        AbsorptionSignal.Name = "Absorption";
        AbsorptionSignal.DrawStyle = DRAWSTYLE_BACKGROUND;
        AbsorptionSignal.PrimaryColor = RGB(70, 70, 120);
        AbsorptionSignal.DrawZeros = false;

        EfficiencyColor.Name = "Efficiency Heatmap";
        EfficiencyColor.DrawStyle = DRAWSTYLE_COLOR_BAR;
        EfficiencyColor.PrimaryColor = RGB(120, 120, 120);
        EfficiencyColor.SecondaryColor = RGB(255, 255, 255);
        EfficiencyColor.DrawZeros = false;

        EnableCluster.Name = "Enable Volume Cluster Collapse";
        EnableCluster.SetYesNo(true);

        EnableDelta.Name = "Enable Delta Engine";
        EnableDelta.SetYesNo(true);

        EnableEfficiency.Name = "Enable Price Efficiency";
        EnableEfficiency.SetYesNo(true);

        EnableTickVolume.Name = "Enable Tick vs Volume Metrics";
        EnableTickVolume.SetYesNo(true);

        EnableATR.Name = "Enable ATR Context";
        EnableATR.SetYesNo(true);

        EnableTraps.Name = "Enable Trap Detection";
        EnableTraps.SetYesNo(true);

        EnableAbsorption.Name = "Enable Absorption Detection";
        EnableAbsorption.SetYesNo(true);

        EnableDebugLogging.Name = "Enable Debug Logging";
        EnableDebugLogging.SetYesNo(false);

        DeltaTrapThreshold.Name = "Delta Trap Threshold";
        DeltaTrapThreshold.SetFloat(0.70f);
        DeltaTrapThreshold.SetFloatLimits(0.0f, 1.0f);

        ExtremeDeltaThreshold.Name = "Extreme Delta Threshold";
        ExtremeDeltaThreshold.SetFloat(0.90f);
        ExtremeDeltaThreshold.SetFloatLimits(0.0f, 1.0f);

        MinimumVolumeForDeltaSignal.Name = "Minimum Volume For Delta Signal";
        MinimumVolumeForDeltaSignal.SetInt(100);
        MinimumVolumeForDeltaSignal.SetIntLimits(0, INT_MAX);

        ATRLength.Name = "ATR Length";
        ATRLength.SetInt(14);
        ATRLength.SetIntLimits(1, 500);

        ATRMALength.Name = "ATR Moving Average Length";
        ATRMALength.SetInt(20);
        ATRMALength.SetIntLimits(1, 500);

        LowFollowThroughATRFactor.Name = "Low Follow Through ATR Factor";
        LowFollowThroughATRFactor.SetFloat(0.35f);
        LowFollowThroughATRFactor.SetFloatLimits(0.01f, 5.0f);

        AbsorptionVolumeThreshold.Name = "Absorption Volume Threshold";
        AbsorptionVolumeThreshold.SetInt(1000);
        AbsorptionVolumeThreshold.SetIntLimits(0, INT_MAX);

        AbsorptionMinMovementTicks.Name = "Absorption Min Movement In Ticks";
        AbsorptionMinMovementTicks.SetInt(4);
        AbsorptionMinMovementTicks.SetIntLimits(1, 10000);

        AbsorptionUseATR.Name = "Absorption Use ATR Normalized Movement";
        AbsorptionUseATR.SetYesNo(false);

        ShowVWAPLine.Name = "Show VWAP Line";
        ShowVWAPLine.SetYesNo(true);

        ShowPOCLine.Name = "Show POC Line";
        ShowPOCLine.SetYesNo(true);

        ShowTrapLabels.Name = "Show Trap Labels";
        ShowTrapLabels.SetYesNo(false);

        ShowAbsorptionHighlights.Name = "Show Absorption Highlights";
        ShowAbsorptionHighlights.SetYesNo(true);

        ShowEfficiencyHeatmap.Name = "Show Efficiency Heatmap";
        ShowEfficiencyHeatmap.SetYesNo(true);

        ShowExtremeDeltaMarkers.Name = "Show Extreme Delta Markers";
        ShowExtremeDeltaMarkers.SetYesNo(true);

        RequireReversalConfirmation.Name = "Require Reversal Confirmation";
        RequireReversalConfirmation.SetYesNo(false);

        ReversalLookaheadBars.Name = "Reversal Lookahead Bars";
        ReversalLookaheadBars.SetInt(3);
        ReversalLookaheadBars.SetIntLimits(1, 20);

        return;
    }

    const int startIndex = max(1, sc.UpdateStartIndex);
    const int arraySize = sc.ArraySize;

    for (int i = startIndex; i < arraySize; ++i)
    {
        ClusterVWAP[i] = 0.0f;
        ClusterPOC[i] = 0.0f;
        ClusterMidpoint[i] = 0.0f;
        ClusterSkew[i] = 0.0f;
        Delta[i] = 0.0f;
        DeltaPct[i] = 0.0f;
        ExtremeDeltaMarker[i] = 0.0f;
        Efficiency[i] = 0.0f;
        ATR[i] = 0.0f;
        ATRMA[i] = 0.0f;
        VolumePerTrade[i] = 0.0f;
        LongTrapSignal[i] = 0.0f;
        ShortTrapSignal[i] = 0.0f;
        AbsorptionSignal[i] = 0.0f;
        EfficiencyColor[i] = 0.0f;

        const double totalVolume = static_cast<double>(sc.BaseData[SC_VOLUME][i]);
        const double askVolume = static_cast<double>(sc.BaseData[SC_ASKVOL][i]);
        const double bidVolume = static_cast<double>(sc.BaseData[SC_BIDVOL][i]);
        const double priceMove = sc.High[i] - sc.Low[i];
        const double markerOffset = max(sc.TickSize * 2.0, priceMove * 0.12);

        ClusterStats cluster;
        cluster.VWAP = sc.Close[i];
        cluster.POC = sc.Close[i];
        cluster.Midpoint = (sc.High[i] + sc.Low[i]) * 0.5;
        cluster.Skew = cluster.VWAP - cluster.Midpoint;

        if (EnableCluster.GetYesNo())
            cluster = CalculateClusterStats(sc, i);

        ClusterVWAP[i] = ShowVWAPLine.GetYesNo() ? static_cast<float>(cluster.VWAP) : 0.0f;
        ClusterPOC[i] = ShowPOCLine.GetYesNo() ? static_cast<float>(cluster.POC) : 0.0f;
        ClusterMidpoint[i] = static_cast<float>(cluster.Midpoint);
        ClusterSkew[i] = static_cast<float>(cluster.Skew);

        double delta = 0.0;
        double deltaPct = 0.0;

        if (EnableDelta.GetYesNo())
        {
            delta = askVolume - bidVolume;
            deltaPct = SafeDiv(delta, totalVolume);
            Delta[i] = static_cast<float>(delta);
            DeltaPct[i] = static_cast<float>(deltaPct);

            if (ShowExtremeDeltaMarkers.GetYesNo() && fabs(deltaPct) >= ExtremeDeltaThreshold.GetFloat() && totalVolume >= MinimumVolumeForDeltaSignal.GetInt())
                ExtremeDeltaMarker[i] = static_cast<float>(sc.High[i] + markerOffset);
        }

        double atr = 0.0;
        if (EnableATR.GetYesNo())
        {
            atr = ManualATR(sc, i, ATRLength.GetInt());
            ATR[i] = static_cast<float>(atr);
            ATRMA[i] = static_cast<float>(SimpleAverage(ATR, i, ATRMALength.GetInt()));
        }

        double efficiency = 0.0;
        if (EnableEfficiency.GetYesNo())
        {
            efficiency = SafeDiv(priceMove, totalVolume);
            Efficiency[i] = static_cast<float>(efficiency);

            if (ShowEfficiencyHeatmap.GetYesNo())
            {
                const double atrNormalized = SafeDiv(priceMove, atr);
                EfficiencyColor[i] = static_cast<float>(sc.Close[i]);

                if (atrNormalized >= 0.75)
                    EfficiencyColor.DataColor[i] = RGB(255, 255, 255);
                else if (atrNormalized >= 0.35)
                    EfficiencyColor.DataColor[i] = RGB(150, 150, 150);
                else
                    EfficiencyColor.DataColor[i] = RGB(80, 80, 80);
            }
        }

        if (EnableTickVolume.GetYesNo())
        {
            const double tickCount = static_cast<double>(sc.NumberOfTrades[i]);
            VolumePerTrade[i] = static_cast<float>(SafeDiv(totalVolume, tickCount));
        }

        bool absorption = false;
        if (EnableAbsorption.GetYesNo())
        {
            const bool highVolume = totalVolume >= AbsorptionVolumeThreshold.GetInt();
            const bool lowMovementTicks = priceMove <= AbsorptionMinMovementTicks.GetInt() * sc.TickSize;
            const bool lowMovementATR = atr > 0.0 && priceMove <= atr * LowFollowThroughATRFactor.GetFloat();

            absorption = highVolume && (AbsorptionUseATR.GetYesNo() ? lowMovementATR : lowMovementTicks);

            if (absorption && ShowAbsorptionHighlights.GetYesNo())
                AbsorptionSignal[i] = 1.0f;
        }

        if (EnableTraps.GetYesNo() && EnableDelta.GetYesNo())
        {
            bool lowFollowThrough = false;
            if (atr > 0.0)
                lowFollowThrough = priceMove <= atr * LowFollowThroughATRFactor.GetFloat();
            else
                lowFollowThrough = priceMove <= AbsorptionMinMovementTicks.GetInt() * sc.TickSize;

            bool longTrap = deltaPct >= DeltaTrapThreshold.GetFloat()
                && sc.Close[i] < cluster.VWAP
                && lowFollowThrough
                && totalVolume >= MinimumVolumeForDeltaSignal.GetInt();

            bool shortTrap = deltaPct <= -DeltaTrapThreshold.GetFloat()
                && sc.Close[i] > cluster.VWAP
                && lowFollowThrough
                && totalVolume >= MinimumVolumeForDeltaSignal.GetInt();

            if (RequireReversalConfirmation.GetYesNo())
            {
                const int lookahead = ReversalLookaheadBars.GetInt();
                bool longConfirmed = false;
                bool shortConfirmed = false;

                for (int j = 1; j <= lookahead && i + j < arraySize; ++j)
                {
                    if (sc.Close[i + j] < sc.Close[i])
                        longConfirmed = true;
                    if (sc.Close[i + j] > sc.Close[i])
                        shortConfirmed = true;
                }

                longTrap = longTrap && longConfirmed;
                shortTrap = shortTrap && shortConfirmed;
            }

            if (longTrap)
            {
                LongTrapSignal[i] = static_cast<float>(sc.High[i] + markerOffset);

                if (ShowTrapLabels.GetYesNo())
                {
                    s_UseTool tool;
                    tool.Clear();
                    tool.ChartNumber = sc.ChartNumber;
                    tool.DrawingType = DRAWING_TEXT;
                    tool.Region = 0;
                    tool.BeginIndex = i;
                    tool.BeginValue = sc.High[i] + markerOffset * 2.0;
                    tool.Color = RGB(255, 80, 80);
                    tool.FontSize = 8;
                    tool.AddMethod = UTAM_ADD_OR_ADJUST;
                    tool.LineNumber = 100000 + i;
                    tool.Text = "TRAP LONG";
                    sc.UseTool(tool);
                }
            }

            if (shortTrap)
            {
                ShortTrapSignal[i] = static_cast<float>(sc.Low[i] - markerOffset);

                if (ShowTrapLabels.GetYesNo())
                {
                    s_UseTool tool;
                    tool.Clear();
                    tool.ChartNumber = sc.ChartNumber;
                    tool.DrawingType = DRAWING_TEXT;
                    tool.Region = 0;
                    tool.BeginIndex = i;
                    tool.BeginValue = sc.Low[i] - markerOffset * 2.0;
                    tool.Color = RGB(80, 255, 120);
                    tool.FontSize = 8;
                    tool.AddMethod = UTAM_ADD_OR_ADJUST;
                    tool.LineNumber = 200000 + i;
                    tool.Text = "TRAP SHORT";
                    sc.UseTool(tool);
                }
            }
        }

        if (EnableDebugLogging.GetYesNo() && sc.GetBarHasClosedStatus(i) == BHCS_BAR_HAS_CLOSED)
        {
            SCString msg;
            msg.Format("AOFTA bar=%d vol=%.0f delta=%.0f deltaPct=%.3f vwap=%.5f poc=%.5f eff=%.8f atr=%.5f absorption=%d",
                i,
                totalVolume,
                delta,
                deltaPct,
                cluster.VWAP,
                cluster.POC,
                efficiency,
                atr,
                absorption ? 1 : 0);
            sc.AddMessageToLog(msg, 0);
        }
    }
}
