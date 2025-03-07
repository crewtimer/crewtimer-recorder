import { UseDatum } from 'react-usedatum';
import Markdown from '../components/Markdown';
import PrivacyMarkdown from '../doc/PrivacyMarkdown.md';
import CrewTimerVideoRecorder from '../doc/CrewTimerVideoRecorderHelp.md';
import RecorderConfig from '../recorder/RecorderConfig';
import RecordingLogTable from './RecordingLogTable';
import { FullSizeWindow } from '../components/FullSizeWindow';
import RGBAImageCanvas from '../components/RGBAImageCanvas';
import ViscaControlPanel from '../visca/ViscaControlPanel';

export const [useSelectedPage] = UseDatum<string>('/');
const MainPage = () => {
  const [page] = useSelectedPage();
  switch (page) {
    case '/':
      return <RecorderConfig />;
    case '/home':
      return <RecorderConfig />;
    case '/log':
      return <RecordingLogTable />;
    case '/camcontrol':
      return <ViscaControlPanel />;
    case '/video':
      return <FullSizeWindow component={RGBAImageCanvas} />;
    case '/privacy':
      return <Markdown md={PrivacyMarkdown} />;
    case '/help':
      return <Markdown md={CrewTimerVideoRecorder} />;
    case '/index.html':
      return <RecorderConfig />;
    default:
      return <RecorderConfig />;
  }
};

export default MainPage;
