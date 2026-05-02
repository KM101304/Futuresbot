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

static double AOFTA_SafeDiv(const double numerator, const double denominator)
{
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

static double AOFTA_Clamp(const double value, const double low, const double high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static double AOFTA_ManualATR(SCStudyInterfaceRef sc, const int index, const int length)
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

static double AOFTA_SimpleAverage(SCFloatArrayRef values, const int index, const int length)
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

static ClusterStats AOFTA_CalculateClusterStats(SCStudyInterfaceRef sc, const int index)
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

static void AOFTA_DrawText(SCStudyInterfaceRef sc, const int lineNumber, const int index, const double value, const COLORREF color, const SCString& text)
{
    s_UseTool tool;
    tool.Clear();
    tool.ChartNumber = sc.ChartNumber;
    tool.DrawingType = DRAWING_TEXT;
    tool.Region = 0;
    tool.BeginIndex = index;
    tool.BeginValue = value;
    tool.Color = color;
    tool.FontSize = 8;
    tool.AddMethod = UTAM_ADD_OR_ADJUST;
    tool.LineNumber = lineNumber;
    tool.Text = text;
    sc.UseTool(tool);
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
    SCSubgraphRef CumulativeDelta = sc.Subgraph[15];
    SCSubgraphRef SessionVWAP = sc.Subgraph[16];
    SCSubgraphRef TrapScore = sc.Subgraph[17];
    SCSubgraphRef ConfirmedLongTrap = sc.Subgraph[18];
    SCSubgraphRef ConfirmedShortTrap = sc.Subgraph[19];

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
    SCInputRef EnableCumulativeDelta = sc.Input[25];
    SCInputRef EnableSessionVWAP = sc.Input[26];
    SCInputRef MinimumTrapScore = sc.Input[27];
    SCInputRef EnableAlerts = sc.Input[28];
    SCInputRef AlertNumber = sc.Input[29];
    SCInputRef ScoreAbsorptionBonus = sc.Input[30];
    SCInputRef ScoreExtremeDeltaBonus = sc.Input[31];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Advanced Order Flow Trap Analyzer v2";
        sc.StudyDescription = "Production ACSIL order-flow study with VAP VWAP/POC, delta, efficiency, ATR, cumulative delta, session VWAP, absorption, trap scoring, alerts, and confirmation.";
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

        LongTrapSignal.Name = "Potential Long Trap";
        LongTrapSignal.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        LongTrapSignal.PrimaryColor = RGB(255, 120, 120);
        LongTrapSignal.LineWidth = 3;
        LongTrapSignal.DrawZeros = false;

        ShortTrapSignal.Name = "Potential Short Trap";
        ShortTrapSignal.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        ShortTrapSignal.PrimaryColor = RGB(120, 255, 140);
        ShortTrapSignal.LineWidth = 3;
        ShortTrapSignal.DrawZeros = false;

        AbsorptionSignal.Name = "Absorption";
        AbsorptionSignal.DrawStyle = DRAWSTYLE_BACKGROUND;
        AbsorptionSignal.PrimaryColor = RGB(70, 70, 120);
        AbsorptionSignal.DrawZeros = false;

        EfficiencyColor.Name = "Efficiency Heatmap";
        EfficiencyColor.DrawStyle = DRAWSTYLE_COLOR_BAR;
        EfficiencyColor.PrimaryColor = RGB(120, 120, 120);
        EfficiencyColor.DrawZeros = false;

        CumulativeDelta.Name = "Cumulative Delta";
        CumulativeDelta.DrawStyle = DRAWSTYLE_IGNORE;
        CumulativeDelta.DrawZeros = true;

        SessionVWAP.Name = "Session VWAP";
        SessionVWAP.DrawStyle = DRAWSTYLE_LINE;
        SessionVWAP.PrimaryColor = RGB(180, 120, 255);
        SessionVWAP.LineWidth = 2;
        SessionVWAP.DrawZeros = false;

        TrapScore.Name = "Trap Score";
        TrapScore.DrawStyle = DRAWSTYLE_IGNORE;
        TrapScore.DrawZeros = true;

        ConfirmedLongTrap.Name = "Confirmed Long Trap";
        ConfirmedLongTrap.DrawStyle = DRAWSTYLE_TRIANGLE_DOWN;
        ConfirmedLongTrap.PrimaryColor = RGB(255, 0, 0);
        ConfirmedLongTrap.LineWidth = 5;
        ConfirmedLongTrap.DrawZeros = false;

        ConfirmedShortTrap.Name = "Confirmed Short Trap";
        ConfirmedShortTrap.DrawStyle = DRAWSTYLE_TRIANGLE_UP;
        ConfirmedShortTrap.PrimaryColor = RGB(0, 255, 80);
        ConfirmedShortTrap.LineWidth = 5;
        ConfirmedShortTrap.DrawZeros = false;

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
        ShowVWAPLine.Name = "Show Cluster VWAP Line";
        ShowVWAPLine.SetYesNo(true);
        ShowPOCLine.Name = "Show Cluster POC Line";
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
        RequireReversalConfirmation.SetYesNo(true);
        ReversalLookaheadBars.Name = "Reversal Lookahead Bars";
        ReversalLookaheadBars.SetInt(3);
        ReversalLookaheadBars.SetIntLimits(1, 20);
        EnableCumulativeDelta.Name = "Enable Cumulative Delta";
        EnableCumulativeDelta.SetYesNo(true);
        EnableSessionVWAP.Name = "Enable Session VWAP";
        EnableSessionVWAP.SetYesNo(true);
        MinimumTrapScore.Name = "Minimum Trap Score";
        MinimumTrapScore.SetFloat(3.0f);
        MinimumTrapScore.SetFloatLimits(1.0f, 8.0f);
        EnableAlerts.Name = "Enable Alerts";
        EnableAlerts.SetYesNo(false);
        AlertNumber.Name = "Alert Number";
        AlertNumber.SetInt(1);
        AlertNumber.SetIntLimits(1, 150);
        ScoreAbsorptionBonus.Name = "Score Absorption Bonus";
        ScoreAbsorptionBonus.SetFloat(1.0f);
        ScoreExtremeDeltaBonus.Name = "Score Extreme Delta Bonus";
        ScoreExtremeDeltaBonus.SetFloat(0.5f);

        return;
    }

    const int startIndex = max(1, sc.UpdateStartIndex);
    const int arraySize = sc.ArraySize;
    double sessionPV = 0.0;
    double sessionVol = 0.0;

    if (startIndex > 1 && EnableSessionVWAP.GetYesNo())
    {
        for (int k = 0; k < startIndex; ++k)
        {
            if (k > 0 && sc.BaseDateTimeIn[k].GetDate() != sc.BaseDateTimeIn[k - 1].GetDate())
            {
                sessionPV = 0.0;
                sessionVol = 0.0;
            }
            const double v = sc.BaseData[SC_VOLUME][k];
            const double typical = (sc.High[k] + sc.Low[k] + sc.Close[k]) / 3.0;
            sessionPV += typical * v;
            sessionVol += v;
        }
    }

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
        TrapScore[i] = 0.0f;
        ConfirmedLongTrap[i] = 0.0f;
        ConfirmedShortTrap[i] = 0.0f;
        if (i == 1)
            CumulativeDelta[i - 1] = 0.0f;

        const double totalVolume = static_cast<double>(sc.BaseData[SC_VOLUME][i]);
        const double askVolume = static_cast<double>(sc.BaseData[SC_ASKVOL][i]);
        const double bidVolume = static_cast<double>(sc.BaseData[SC_BIDVOL][i]);
        const double priceMove = sc.High[i] - sc.Low[i];
        const double markerOffset = max(sc.TickSize * 2.0, priceMove * 0.12);
        const double typicalPrice = (sc.High[i] + sc.Low[i] + sc.Close[i]) / 3.0;

        ClusterStats cluster;
        cluster.VWAP = sc.Close[i];
        cluster.POC = sc.Close[i];
        cluster.Midpoint = (sc.High[i] + sc.Low[i]) * 0.5;
        cluster.Skew = cluster.VWAP - cluster.Midpoint;

        if (EnableCluster.GetYesNo())
            cluster = AOFTA_CalculateClusterStats(sc, i);

        ClusterVWAP[i] = ShowVWAPLine.GetYesNo() ? static_cast<float>(cluster.VWAP) : 0.0f;
        ClusterPOC[i] = ShowPOCLine.GetYesNo() ? static_cast<float>(cluster.POC) : 0.0f;
        ClusterMidpoint[i] = static_cast<float>(cluster.Midpoint);
        ClusterSkew[i] = static_cast<float>(cluster.Skew);

        double delta = 0.0;
        double deltaPct = 0.0;
        bool extremeDelta = false;

        if (EnableDelta.GetYesNo())
        {
            delta = askVolume - bidVolume;
            deltaPct = AOFTA_SafeDiv(delta, totalVolume);
            extremeDelta = fabs(deltaPct) >= ExtremeDeltaThreshold.GetFloat() && totalVolume >= MinimumVolumeForDeltaSignal.GetInt();
            Delta[i] = static_cast<float>(delta);
            DeltaPct[i] = static_cast<float>(deltaPct);

            if (EnableCumulativeDelta.GetYesNo())
                CumulativeDelta[i] = (i > 0 ? CumulativeDelta[i - 1] : 0.0f) + static_cast<float>(delta);

            if (ShowExtremeDeltaMarkers.GetYesNo() && extremeDelta)
                ExtremeDeltaMarker[i] = static_cast<float>(sc.High[i] + markerOffset);
        }

        if (EnableSessionVWAP.GetYesNo())
        {
            if (i > 0 && sc.BaseDateTimeIn[i].GetDate() != sc.BaseDateTimeIn[i - 1].GetDate())
            {
                sessionPV = 0.0;
                sessionVol = 0.0;
                if (EnableCumulativeDelta.GetYesNo())
                    CumulativeDelta[i] = static_cast<float>(delta);
            }

            sessionPV += typicalPrice * totalVolume;
            sessionVol += totalVolume;
            SessionVWAP[i] = static_cast<float>(AOFTA_SafeDiv(sessionPV, sessionVol));
        }
        else
        {
            SessionVWAP[i] = 0.0f;
        }

        double atr = 0.0;
        if (EnableATR.GetYesNo())
        {
            atr = AOFTA_ManualATR(sc, i, ATRLength.GetInt());
            ATR[i] = static_cast<float>(atr);
            ATRMA[i] = static_cast<float>(AOFTA_SimpleAverage(ATR, i, ATRMALength.GetInt()));
        }

        double efficiency = 0.0;
        double atrNormalizedMove = 0.0;
        if (EnableEfficiency.GetYesNo())
        {
            efficiency = AOFTA_SafeDiv(priceMove, totalVolume);
            atrNormalizedMove = AOFTA_SafeDiv(priceMove, atr);
            Efficiency[i] = static_cast<float>(efficiency);

            if (ShowEfficiencyHeatmap.GetYesNo())
            {
                EfficiencyColor[i] = static_cast<float>(sc.Close[i]);
                if (atrNormalizedMove >= 0.75)
                    EfficiencyColor.DataColor[i] = RGB(255, 255, 255);
                else if (atrNormalizedMove >= 0.35)
                    EfficiencyColor.DataColor[i] = RGB(150, 150, 150);
                else
                    EfficiencyColor.DataColor[i] = RGB(80, 80, 80);
            }
        }

        if (EnableTickVolume.GetYesNo())
        {
            const double tickCount = static_cast<double>(sc.NumberOfTrades[i]);
            VolumePerTrade[i] = static_cast<float>(AOFTA_SafeDiv(totalVolume, tickCount));
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
            const bool lowFollowThrough = atr > 0.0
                ? priceMove <= atr * LowFollowThroughATRFactor.GetFloat()
                : priceMove <= AbsorptionMinMovementTicks.GetInt() * sc.TickSize;

            const bool highEnoughVolume = totalVolume >= MinimumVolumeForDeltaSignal.GetInt();
            const bool buyersFailed = deltaPct >= DeltaTrapThreshold.GetFloat() && sc.Close[i] < cluster.VWAP;
            const bool sellersFailed = deltaPct <= -DeltaTrapThreshold.GetFloat() && sc.Close[i] > cluster.VWAP;

            double score = 0.0;
            if (fabs(deltaPct) >= DeltaTrapThreshold.GetFloat()) score += 1.0;
            if (buyersFailed || sellersFailed) score += 1.0;
            if (lowFollowThrough) score += 1.0;
            if (highEnoughVolume) score += 1.0;
            if (absorption) score += ScoreAbsorptionBonus.GetFloat();
            if (extremeDelta) score += ScoreExtremeDeltaBonus.GetFloat();
            if (EnableSessionVWAP.GetYesNo() && SessionVWAP[i] != 0.0f && fabs(sc.Close[i] - SessionVWAP[i]) <= max(sc.TickSize * 8.0, atr * 0.25)) score += 0.5;
            score = AOFTA_Clamp(score, 0.0, 8.0);
            TrapScore[i] = static_cast<float>(score);

            bool longTrap = buyersFailed && lowFollowThrough && highEnoughVolume && score >= MinimumTrapScore.GetFloat();
            bool shortTrap = sellersFailed && lowFollowThrough && highEnoughVolume && score >= MinimumTrapScore.GetFloat();

            bool longConfirmed = !RequireReversalConfirmation.GetYesNo();
            bool shortConfirmed = !RequireReversalConfirmation.GetYesNo();

            if (RequireReversalConfirmation.GetYesNo())
            {
                const int lookahead = ReversalLookaheadBars.GetInt();
                longConfirmed = false;
                shortConfirmed = false;

                for (int j = 1; j <= lookahead && i + j < arraySize; ++j)
                {
                    if (sc.Close[i + j] < sc.Low[i])
                        longConfirmed = true;
                    if (sc.Close[i + j] > sc.High[i])
                        shortConfirmed = true;
                }
            }

            if (longTrap)
            {
                LongTrapSignal[i] = static_cast<float>(sc.High[i] + markerOffset);
                if (longConfirmed)
                    ConfirmedLongTrap[i] = static_cast<float>(sc.High[i] + markerOffset * 1.8);
                if (ShowTrapLabels.GetYesNo())
                    AOFTA_DrawText(sc, 100000 + i, i, sc.High[i] + markerOffset * 2.6, RGB(255, 80, 80), longConfirmed ? "CONF LONG TRAP" : "LONG TRAP");
                if (EnableAlerts.GetYesNo() && longConfirmed && sc.GetBarHasClosedStatus(i) == BHCS_BAR_HAS_CLOSED)
                    sc.SetAlert(AlertNumber.GetInt(), "AOFTA Confirmed Long Trap");
            }

            if (shortTrap)
            {
                ShortTrapSignal[i] = static_cast<float>(sc.Low[i] - markerOffset);
                if (shortConfirmed)
                    ConfirmedShortTrap[i] = static_cast<float>(sc.Low[i] - markerOffset * 1.8);
                if (ShowTrapLabels.GetYesNo())
                    AOFTA_DrawText(sc, 200000 + i, i, sc.Low[i] - markerOffset * 2.6, RGB(80, 255, 120), shortConfirmed ? "CONF SHORT TRAP" : "SHORT TRAP");
                if (EnableAlerts.GetYesNo() && shortConfirmed && sc.GetBarHasClosedStatus(i) == BHCS_BAR_HAS_CLOSED)
                    sc.SetAlert(AlertNumber.GetInt(), "AOFTA Confirmed Short Trap");
            }
        }

        if (EnableDebugLogging.GetYesNo() && sc.GetBarHasClosedStatus(i) == BHCS_BAR_HAS_CLOSED)
        {
            SCString msg;
            msg.Format("AOFTA bar=%d vol=%.0f delta=%.0f deltaPct=%.3f vwap=%.5f poc=%.5f sessionVWAP=%.5f eff=%.8f atr=%.5f score=%.2f absorption=%d",
                i, totalVolume, delta, deltaPct, cluster.VWAP, cluster.POC, SessionVWAP[i], efficiency, atr, TrapScore[i], AbsorptionSignal[i] != 0.0f ? 1 : 0);
            sc.AddMessageToLog(msg, 0);
        }
    }
}
