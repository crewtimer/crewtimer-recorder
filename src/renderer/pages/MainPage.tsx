import { UseDatum } from 'react-usedatum';
import Markdown from '../components/Markdown';
import PrivacyMarkdown from '../doc/PrivacyMarkdown.md';
import CrewTimerVideoRecorder from '../doc/CrewTimerVideoRecorderHelp.md';
import RecorderConfig from '../recorder/RecorderConfig';
import RecordingLogTable from './RecordingLogTable';
import { FullScreenVideo } from '../recorder/FullScreenVideo';

export const [useSelectedPage] = UseDatum<string>('/');

const SETTINGS_PAGES = ['/', '/home', '/index.html'];

const MainPage = () => {
  const [page] = useSelectedPage();

  if (page === '/privacy') return <Markdown md={PrivacyMarkdown} />;
  if (page === '/help') return <Markdown md={CrewTimerVideoRecorder} />;

  const isSettings = SETTINGS_PAGES.includes(page);

  return (
    <>
      {/* Keep RecorderConfig always mounted so stream management effects are never torn down on tab switch */}
      <div
        style={{
          display: isSettings ? 'flex' : 'none',
          flexDirection: 'column',
          height: '100%',
        }}
      >
        <RecorderConfig showPreview={isSettings} />
      </div>
      {page === '/log' && <RecordingLogTable />}
      {page === '/video' && <FullScreenVideo />}
    </>
  );
};

export default MainPage;
